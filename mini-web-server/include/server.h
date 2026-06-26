#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

#include "http_core.h"
#include "router.h"
#include "middleware.h"

/*
 * L1 - Core Definitions: Server, ThreadPool, Connection structs
 * L2 - Core Concepts: TCP connection lifecycle, keep-alive, event-driven I/O
 * L3 - Engineering Structures: Thread pool with mutex/condvar work queue
 * L4 - Standards/Theorems: Little's Law (L = lambda*W) for queuing analysis,
 *      Amdahl's Law for parallel speedup limits
 * L5 - Algorithms: select()-based I/O multiplexing event loop
 * L6 - Canonical Problem: Web Server - the canonical backend problem
 * L7 - Application: Multi-threaded HTTP server with keep-alive
 * L8 - Advanced: Thread pool work-stealing patterns, backpressure
 * L9 - Industry: Event-driven architecture (comparable to nginx/lighttpd)
 */

#define SERVER_MAX_CONNECTIONS    1024
#define SERVER_RECV_BUF_SIZE     65536
#define SERVER_DEFAULT_PORT       8080
#define SERVER_DEFAULT_BACKLOG     128
#define SERVER_DEFAULT_THREADS       4
#define SERVER_KEEPALIVE_TIMEOUT    15
#define SERVER_MAX_EVENTS          256
#define POOL_MAX_QUEUE_SIZE       512

typedef struct {
    int    fd;
    time_t last_activity;
    char   recv_buf[SERVER_RECV_BUF_SIZE];
    size_t recv_len;
    HttpRequest  request;
    HttpResponse response;
    bool   keep_alive;
    bool   closed;
} Connection;

typedef struct {
    Connection       *conn;
    const Router     *router;
    MiddlewareChain  *middleware;
} WorkItem;

typedef struct {
    pthread_t    *workers;
    int           worker_count;
    WorkItem     *queue;
    int           queue_capacity;
    int           queue_front;
    int           queue_rear;
    int           queue_size;
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    bool            running;
} ThreadPool;

typedef struct {
    int              listen_fd;
    int              port;
    int              backlog;
    int              max_connections;
    int              thread_count;
    int              keepalive_timeout;
    Connection      *connections;
    int              conn_count;
    int              conn_capacity;
    Router          *router;
    MiddlewareChain  middleware;
    ThreadPool       pool;
    bool             running;
} Server;

void server_init(Server *srv, int port);
void server_set_router(Server *srv, Router *router);
void server_set_middleware_chain(Server *srv, MiddlewareChain *chain);
int  server_start(Server *srv);
void server_stop(Server *srv);
void server_destroy(Server *srv);

bool thread_pool_create(ThreadPool *pool, int num_workers);
bool thread_pool_submit(ThreadPool *pool, WorkItem *item);
void thread_pool_destroy(ThreadPool *pool);

Connection *server_add_connection(Server *srv, int client_fd);
void server_remove_connection(Server *srv, int index);
int  server_reap_idle_connections(Server *srv);

#endif /* SERVER_H */
