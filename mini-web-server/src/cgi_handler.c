#include "cgi_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

void cgi_config_init(CgiConfig *cfg, const char *script_path) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->script_path, script_path, CGI_MAX_SCRIPT_PATH - 1);
    cfg->pass_headers   = true;
    cfg->pass_body      = true;
    cfg->timeout_seconds = CGI_TIMEOUT_SECONDS;
}

void cgi_config_add_env(CgiConfig *cfg, const char *key, const char *value) {
    if (cfg->env_count >= CGI_MAX_ENV_VARS) return;
    strncpy(cfg->env_vars[cfg->env_count].key, key, CGI_MAX_ENV_KEY - 1);
    strncpy(cfg->env_vars[cfg->env_count].value, value, CGI_MAX_ENV_VALUE - 1);
    cfg->env_count++;
}

void cgi_config_set_request_env(CgiConfig *cfg, const HttpRequest *req) {
    cgi_config_add_env(cfg, "REQUEST_METHOD", http_method_str(req->method));
    cgi_config_add_env(cfg, "REQUEST_URI", req->path);

    char qs[HTTP_MAX_PATH];
    snprintf(qs, sizeof(qs), "%s", req->query_string[0] ? req->query_string : "");
    cgi_config_add_env(cfg, "QUERY_STRING", qs);

    cgi_config_add_env(cfg, "GATEWAY_INTERFACE", "CGI/1.1");
    cgi_config_add_env(cfg, "SERVER_PROTOCOL", "HTTP/1.1");
    cgi_config_add_env(cfg, "SERVER_SOFTWARE", "mini-web-server/0.1");

    const char *ct = http_request_get_header(req, "Content-Type");
    if (ct) cgi_config_add_env(cfg, "CONTENT_TYPE", ct);

    const char *cl = http_request_get_header(req, "Content-Length");
    if (cl) cgi_config_add_env(cfg, "CONTENT_LENGTH", cl);

    const char *host = http_request_get_header(req, "Host");
    if (host) cgi_config_add_env(cfg, "HTTP_HOST", host);

    const char *ua = http_request_get_header(req, "User-Agent");
    if (ua) cgi_config_add_env(cfg, "HTTP_USER_AGENT", ua);
}

static int wait_with_timeout(pid_t pid, int timeout_sec) {
    int status = 0;
    int waited = 0;
    while (waited < timeout_sec) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) return status;
        if (w < 0 && errno != EINTR) return -1;
        sleep(1);
        waited++;
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return -2;
}

bool cgi_execute(const CgiConfig *cfg, const HttpRequest *req,
                  CgiResult *result) {
    if (!cfg || !req || !result) return false;

    memset(result, 0, sizeof(*result));

    int stdin_pipe[2];  /* parent writes body to child's stdin  */
    int stdout_pipe[2]; /* child writes response to parent      */
    int stderr_pipe[2];

    if (pipe(stdin_pipe) < 0) return false;
    if (pipe(stdout_pipe) < 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); return false; }
    if (pipe(stderr_pipe) < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return false;
    }

    if (pid == 0) {
        /* Child */
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);  close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);

        for (int i = 0; i < cfg->env_count; i++) {
            setenv(cfg->env_vars[i].key, cfg->env_vars[i].value, 1);
        }

        char *argv[] = { (char *)cfg->script_path, NULL };
        execv(cfg->script_path, argv);
        _exit(EXIT_FAILURE);
    }

    /* Parent */
    close(stdin_pipe[0]); close(stdout_pipe[1]); close(stderr_pipe[1]);

    if (req->body && req->body_len > 0 && cfg->pass_body) {
        ssize_t wrote = write(stdin_pipe[1], req->body, req->body_len);
        (void)wrote;
    }
    close(stdin_pipe[1]);

    ssize_t nr = read(stdout_pipe[0], result->stdout_data,
                      CGI_MAX_OUTPUT - 1);
    if (nr > 0) {
        result->stdout_data[nr] = '\0';
        result->stdout_len = (size_t)nr;
    }

    ssize_t ne = read(stderr_pipe[0], result->stderr_data,
                      CGI_MAX_OUTPUT - 1);
    if (ne > 0) {
        result->stderr_data[ne] = '\0';
        result->stderr_len = (size_t)ne;
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status = wait_with_timeout(pid, cfg->timeout_seconds);
    if (status == -2) {
        result->timed_out = true;
        result->exit_code = -1;
    } else if (WIFEXITED(status)) {
        result->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result->signal_number = WTERMSIG(status);
        result->exit_code = 128 + result->signal_number;
    } else {
        result->exit_code = -1;
    }

    return true;
}

bool cgi_parse_status_line(const char *data, size_t len, int *status_code,
                            char *status_text, size_t text_sz) {
    if (!data || len < 12) return false;
    if (strncmp(data, "HTTP/", 5) != 0 && strncmp(data, "Status:", 7) != 0)
        return false;

    if (data[0] == 'S') {
        return sscanf(data, "Status: %d %127[^\r\n]", status_code,
                      status_text) >= 1;
    }
    return sscanf(data, "HTTP/1.%*c %d %127[^\r\n]", status_code,
                  status_text) >= 1;
}

void cgi_parse_headers(const CgiResult *result, HttpResponse *res) {
    const char *p = result->stdout_data;
    const char *end = p + result->stdout_len;
    while (p < end) {
        if (p[0] == '\r' && p + 1 < end && p[1] == '\n') break;
        if (p[0] == '\n') break;

        const char *colon = strchr(p, ':');
        if (!colon) break;

        char name[128] = {0};
        size_t nlen = (size_t)(colon - p);
        if (nlen > sizeof(name) - 1) nlen = sizeof(name) - 1;
        memcpy(name, p, nlen);

        const char *v = colon + 1;
        while (*v == ' ') v++;

        const char *line_end = strstr(v, "\r\n");
        if (!line_end) line_end = strchr(v, '\n');
        if (!line_end) line_end = v + strlen(v);

        char value[4096] = {0};
        size_t vlen = (size_t)(line_end - v);
        if (vlen > sizeof(value) - 1) vlen = sizeof(value) - 1;
        memcpy(value, v, vlen);

        http_response_add_header(res, name, value);

        const char *next = (*line_end == '\r') ? line_end + 2 : line_end + 1;
        p = next;
    }
}

bool cgi_result_to_response(const CgiResult *result, HttpResponse *res) {
    if (!result || !res || result->stdout_len == 0) return false;

    int cgi_status = HTTP_STATUS_OK;
    char cgi_status_text[128] = {0};
    if (cgi_parse_status_line(result->stdout_data, result->stdout_len,
                               &cgi_status, cgi_status_text,
                               sizeof(cgi_status_text))) {
        http_response_set_status(res, cgi_status);
    }

    cgi_parse_headers(result, res);

    const char *body_start = strstr(result->stdout_data, "\r\n\r\n");
    if (!body_start) body_start = strstr(result->stdout_data, "\n\n");
    if (body_start) {
        body_start = (*body_start == '\r') ? body_start + 4 : body_start + 2;
        size_t body_len = result->stdout_len -
                          (size_t)(body_start - result->stdout_data);
        http_response_set_body(res, body_start, body_len);
    }
    return true;
}

/* ── FastCGI Simulation Helpers ─────────────────────────────────────────── */
void fcgi_build_header(uint8_t type, uint16_t request_id,
                        uint16_t content_len, uint8_t padding_len,
                        FcgiHeader *header) {
    header->version        = 1;
    header->type           = type;
    header->request_id     = ((request_id & 0xFF) << 8) | ((request_id >> 8) & 0xFF);
    header->content_length = ((content_len & 0xFF) << 8) | ((content_len >> 8) & 0xFF);
    header->padding_length = padding_len;
    header->reserved       = 0;
}

void fcgi_build_begin_request(uint16_t role, uint8_t flags,
                               FcgiBeginRequestBody *body) {
    body->role = ((role & 0xFF) << 8) | ((role >> 8) & 0xFF);
    body->flags = flags;
    memset(body->reserved, 0, sizeof(body->reserved));
}

bool fcgi_parse_header(const uint8_t *data, size_t len, FcgiHeader *header) {
    if (!data || len < 8) return false;
    header->version     = data[0];
    header->type        = data[1];
    header->request_id  = ((uint16_t)data[2] << 8) | data[3];
    header->content_length = ((uint16_t)data[4] << 8) | data[5];
    header->padding_length = data[6];
    header->reserved    = data[7];
    return header->version == 1;
}

bool fcgi_parse_end_request(const uint8_t *data, FcgiEndRequestBody *body) {
    if (!data) return false;
    body->app_status = ((uint32_t)data[0] << 24) |
                        ((uint32_t)data[1] << 16) |
                        ((uint32_t)data[2] << 8)  |
                        (uint32_t)data[3];
    body->protocol_status = data[4];
    return true;
}
