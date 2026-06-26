#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define CLOSE_SOCKET closesocket
#  define SOCKERR WSAGetLastError()
#  define WOULDBLOCK_ERR WSAEWOULDBLOCK
   typedef int socklen_t;
#else
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <fcntl.h>
#  define CLOSE_SOCKET close
#  define SOCKERR errno
#  define WOULDBLOCK_ERR EAGAIN
#endif

static volatile sig_atomic_t g_server_running = 0;

/*
 * L5: Producer-consumer ring buffer queue.
 * Workers dequeue WorkItems and process HTTP requests through middleware + router.
 * Bounded queue provides backpressure - submit blocks when full.
 */

static void *worker_thread(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);
        while (pool->queue_size == 0 && pool->running) {
            pthread_cond_wait(&pool->not_empty, &pool->mutex);
        }
        if (pool->queue_size == 0 && !pool->running) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        WorkItem item = pool->queue[pool->queue_front];
        pool->queue_front = (pool->queue_front + 1) % pool->queue_capacity;
        pool->queue_size--;
        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->mutex);

        if (item.conn && item.conn->fd >= 0 && !item.conn->closed) {
            MiddlewareContext mctx;
            memset(&mctx, 0, sizeof(mctx));

            bool ok = 1;
            if (item.middleware && item.middleware->count > 0) {
                ok = middleware_chain_execute(item.middleware,
                                               &item.conn->request,
                                               &item.conn->response, &mctx);
            }

            if (ok && item.router) {
                router_dispatch(item.router,
                                item.conn->request.method,
                                item.conn->request.path,
                                &item.conn->request,
                                &item.conn->response);
            }

            char resp_buf[65536];
            int resp_len = http_serialize_response(&item.conn->response,
                                                    resp_buf, sizeof(resp_buf));
            if (resp_len > 0) {
                ssize_t sent = send(item.conn->fd, resp_buf,
                                     (size_t)resp_len, 0);
                (void)sent;
            }

            if (item.conn->keep_alive) {
                http_request_free(&item.conn->request);
                http_response_free(&item.conn->response);
                http_request_init(&item.conn->request);
                http_response_init(&item.conn->response);
                item.conn->recv_len = 0;
                item.conn->last_activity = time(NULL);
            } else {
                CLOSE_SOCKET(item.conn->fd);
                item.conn->fd = -1;
                item.conn->closed = 1;
            }
        }
    }
    return NULL;
}

bool thread_pool_create(ThreadPool *pool, int num_workers) {
    if (!pool || num_workers < 1 || num_workers > 64) return 0;
    memset(pool, 0, sizeof(*pool));
    pool->worker_count   = num_workers;
    pool->queue_capacity = POOL_MAX_QUEUE_SIZE;

    pool->queue = (WorkItem *)calloc((size_t)pool->queue_capacity,
                                      sizeof(WorkItem));
    if (!pool->queue) return 0;

    pool->workers = (pthread_t *)calloc((size_t)num_workers,
                                         sizeof(pthread_t));
    if (!pool->workers) { free(pool->queue); return 0; }

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->not_empty, NULL);
    pthread_cond_init(&pool->not_full, NULL);
    pool->running = 1;

    for (int i = 0; i < num_workers; i++) {
        if (pthread_create(&pool->workers[i], NULL, worker_thread, pool) != 0) {
            pool->running = 0;
            pthread_cond_broadcast(&pool->not_empty);
            for (int j = 0; j < i; j++) {
                pthread_join(pool->workers[j], NULL);
            }
            free(pool->workers); pool->workers = NULL;
            free(pool->queue);   pool->queue   = NULL;
            return 0;
        }
    }
    return 1;
}


bool thread_pool_submit(ThreadPool *pool, WorkItem *item) {
    if (!pool || !item) return 0;
    pthread_mutex_lock(&pool->mutex);
    while (pool->queue_size >= pool->queue_capacity && pool->running) {
        pthread_cond_wait(&pool->not_full, &pool->mutex);
    }
    if (!pool->running) { pthread_mutex_unlock(&pool->mutex); return 0; }
    pool->queue[pool->queue_rear] = *item;
    pool->queue_rear = (pool->queue_rear + 1) % pool->queue_capacity;
    pool->queue_size++;
    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);
    return 1;
}

void thread_pool_destroy(ThreadPool *pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->mutex);
    pool->running = 0;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_cond_broadcast(&pool->not_full);
    pthread_mutex_unlock(&pool->mutex);
    for (int i = 0; i < pool->worker_count; i++) {
        if (pool->workers) pthread_join(pool->workers[i], NULL);
    }
    free(pool->workers);
    free(pool->queue);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
}

/*
 * L2: Connection lifecycle - accept, parse, dispatch, respond, close/keep-alive.
 * Keep-alive (RFC 7230 Sec 6.3) reuses TCP connections to amortize handshake cost.
 * L4: Little's Law applied - max_connections = arrival_rate * avg_service_time.
 */

Connection *server_add_connection(Server *srv, int client_fd) {
    if (!srv || srv->conn_count >= srv->conn_capacity) return NULL;
    Connection *conn = &srv->connections[srv->conn_count];
    memset(conn, 0, sizeof(*conn));
    conn->fd = client_fd;
    conn->last_activity = time(NULL);
    conn->keep_alive = 1;
    conn->closed = 0;
    http_request_init(&conn->request);
    http_response_init(&conn->response);
    srv->conn_count++;
    return conn;
}

void server_remove_connection(Server *srv, int index) {
    if (!srv || index < 0 || index >= srv->conn_count) return;
    Connection *conn = &srv->connections[index];
    if (conn->fd >= 0) { CLOSE_SOCKET(conn->fd); conn->fd = -1; }
    http_request_free(&conn->request);
    http_response_free(&conn->response);
    if (index < srv->conn_count - 1) {
        srv->connections[index] = srv->connections[srv->conn_count - 1];
    }
    srv->conn_count--;
}

int server_reap_idle_connections(Server *srv) {
    if (!srv) return 0;
    int reaped = 0;
    time_t now = time(NULL);
    for (int i = srv->conn_count - 1; i >= 0; i--) {
        Connection *conn = &srv->connections[i];
        if (conn->fd >= 0 && !conn->closed &&
            (now - conn->last_activity) > srv->keepalive_timeout) {
            server_remove_connection(srv, i);
            reaped++;
        }
    }
    return reaped;
}

static bool set_nonblocking(int fd) {
#ifdef _WIN32
    unsigned long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
#endif
}


/*
 * L5: HTTP request parsing from raw TCP buffer.
 * Splits headers by CRLF, extracts request line, then parses headers one by one.
 * Handles edge cases: partial reads, malformed input, missing CRLF, oversize headers.
 */
static int parse_http_request(Connection *conn) {
    char *data = conn->recv_buf;
    size_t len = conn->recv_len;

    char *header_end = strstr(data, "\r\n\r\n");
    if (!header_end) return -1;

    char *line_end = strstr(data, "\r\n");
    if (!line_end) return -1;

    *line_end = '\0';
    bool ok = http_parse_request_line(data, &conn->request);
    *line_end = '\r';
    if (!ok) return -2;

    char *pos = line_end + 2;
    while (pos < header_end && *pos != '\r') {
        char *hdr_end = strstr(pos, "\r\n");
        if (!hdr_end) break;
        *hdr_end = '\0';
        http_parse_header(pos, &conn->request);
        *hdr_end = '\r';
        pos = hdr_end + 2;
    }

    const char *cl_str = http_request_get_header(&conn->request, "Content-Length");
    if (cl_str) {
        long body_len = strtol(cl_str, NULL, 10);
        size_t headers_len = (size_t)(header_end + 4 - data);
        if (body_len > 0 && len >= headers_len + (size_t)body_len) {
            free(conn->request.body);
            conn->request.body = malloc((size_t)body_len + 1);
            if (conn->request.body) {
                memcpy(conn->request.body, data + headers_len, (size_t)body_len);
                conn->request.body[(size_t)body_len] = '\0';
                conn->request.body_len = (size_t)body_len;
            }
        }
    }

    const char *conn_hdr = http_request_get_header(&conn->request, "Connection");
    if (conn_hdr && strcasecmp(conn_hdr, "close") == 0) {
        conn->keep_alive = 0;
    }
    return 0;
}


/*
 * L5: select()-based I/O multiplexing event loop.
 * Single-threaded acceptor dispatches to thread pool workers.
 * L4: Amdahl's Law - event loop is serial portion; pool handles parallel.
 *     Speedup <= 1 / (S + (1-S)/N) where S = serial fraction, N = threads.
 * select() complexity: O(max_fd), acceptable for <= 1024 connections.
 */
static int server_event_loop(Server *srv) {
    fd_set read_fds;
    struct timeval tv;
    int max_fd;

    while (g_server_running) {
        FD_ZERO(&read_fds);
        FD_SET(srv->listen_fd, &read_fds);
        max_fd = srv->listen_fd;

        for (int i = 0; i < srv->conn_count; i++) {
            int fd = srv->connections[i].fd;
            if (fd >= 0 && !srv->connections[i].closed) {
                FD_SET(fd, &read_fds);
                if (fd > max_fd) max_fd = fd;
            }
        }

        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ready < 0) {
            if (SOCKERR == EINTR || SOCKERR == WOULDBLOCK_ERR) continue;
            return -1;
        }

        server_reap_idle_connections(srv);

        if (FD_ISSET(srv->listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(srv->listen_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (client_fd >= 0) {
                set_nonblocking(client_fd);
                int optval = 1;
#ifdef _WIN32
                setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&optval, sizeof(optval));
#else
                setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
#endif
                if (srv->conn_count < srv->conn_capacity) {
                    server_add_connection(srv, client_fd);
                } else {
                    const char *resp = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
                    send(client_fd, resp, strlen(resp), 0);
                    CLOSE_SOCKET(client_fd);
                }
            }
            ready--;
        }

        for (int i = 0; i < srv->conn_count && ready > 0; i++) {
            Connection *conn = &srv->connections[i];
            if (conn->fd < 0 || conn->closed) continue;
            if (!FD_ISSET(conn->fd, &read_fds)) continue;
            ready--;

            ssize_t nread = recv(conn->fd, conn->recv_buf + conn->recv_len,
                                  sizeof(conn->recv_buf) - conn->recv_len - 1, 0);

            if (nread <= 0) {
                server_remove_connection(srv, i);
                i--;
                continue;
            }

            conn->recv_len += (size_t)nread;
            conn->recv_buf[conn->recv_len] = '\0';
            conn->last_activity = time(NULL);

            int parse_rc = parse_http_request(conn);
            if (parse_rc == 0) {
                WorkItem item;
                item.conn       = conn;
                item.router     = srv->router;
                item.middleware = &srv->middleware;
                thread_pool_submit(&srv->pool, &item);
            } else if (parse_rc == -2) {
                http_response_set_status(&conn->response, HTTP_STATUS_BAD_REQUEST);
                http_response_set_body_str(&conn->response, "400 Bad Request");
                char resp_buf[4096];
                int rlen = http_serialize_response(&conn->response, resp_buf, sizeof(resp_buf));
                if (rlen > 0) { send(conn->fd, resp_buf, (size_t)rlen, 0); }
                server_remove_connection(srv, i);
                i--;
            }
        }
    }
    return 0;
}

void server_init(Server *srv, int port) {
    memset(srv, 0, sizeof(*srv));
    srv->port              = port;
    srv->backlog           = SERVER_DEFAULT_BACKLOG;
    srv->max_connections   = SERVER_MAX_CONNECTIONS;
    srv->thread_count      = SERVER_DEFAULT_THREADS;
    srv->keepalive_timeout  = SERVER_KEEPALIVE_TIMEOUT;
    srv->listen_fd         = -1;
    srv->conn_capacity     = SERVER_MAX_CONNECTIONS;
    srv->connections = (Connection *)calloc((size_t)srv->conn_capacity, sizeof(Connection));
    middleware_chain_init(&srv->middleware);
}

void server_set_router(Server *srv, Router *router) {
    if (srv) srv->router = router;
}

void server_set_middleware_chain(Server *srv, MiddlewareChain *chain) {
    if (srv && chain) srv->middleware = *chain;
}

int server_start(Server *srv) {
    if (!srv) return -1;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
#endif
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) return -1;

    int optval = 1;
#ifdef _WIN32
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));
    int nodelay = 1;
    setsockopt(srv->listen_fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
#else
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    int nodelay = 1;
    setsockopt(srv->listen_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
#endif
    set_nonblocking(srv->listen_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)srv->port);

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLOSE_SOCKET(srv->listen_fd); srv->listen_fd = -1; return -1;
    }
    if (listen(srv->listen_fd, srv->backlog) < 0) {
        CLOSE_SOCKET(srv->listen_fd); srv->listen_fd = -1; return -1;
    }

    if (!thread_pool_create(&srv->pool, srv->thread_count)) {
        CLOSE_SOCKET(srv->listen_fd); srv->listen_fd = -1; return -1;
    }

    srv->running = 1;
    g_server_running = 1;
    int rc = server_event_loop(srv);
    thread_pool_destroy(&srv->pool);
#ifdef _WIN32
    WSACleanup();
#endif
    return rc;
}

void server_stop(Server *srv) {
    (void)srv;
    g_server_running = 0;
}

void server_destroy(Server *srv) {
    if (!srv) return;
    if (srv->listen_fd >= 0) { CLOSE_SOCKET(srv->listen_fd); srv->listen_fd = -1; }
    for (int i = 0; i < srv->conn_count; i++) {
        if (srv->connections[i].fd >= 0) {
            CLOSE_SOCKET(srv->connections[i].fd);
        }
        http_request_free(&srv->connections[i].request);
        http_response_free(&srv->connections[i].response);
    }
    free(srv->connections);
    srv->connections = NULL;
}
