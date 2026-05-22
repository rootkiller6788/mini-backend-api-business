#include "middleware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char g_cors_origin[256]  = "*";
static char g_cors_methods[256] = "GET, POST, PUT, DELETE, OPTIONS";
static char g_cors_headers[256] = "Content-Type, Authorization";

void middleware_chain_init(MiddlewareChain *chain) {
    memset(chain, 0, sizeof(*chain));
}

bool middleware_chain_add(MiddlewareChain *chain, MiddlewareFn fn,
                           void *user_ctx) {
    if (!chain || !fn || chain->count >= MIDDLEWARE_MAX_CHAIN) return false;
    chain->entries[chain->count]   = fn;
    chain->contexts[chain->count]  = user_ctx;
    chain->count++;
    return true;
}

bool middleware_chain_execute(MiddlewareChain *chain, HttpRequest *req,
                               HttpResponse *res, MiddlewareContext *mctx) {
    if (!chain || !req || !res) return false;
    for (int i = 0; i < chain->count; i++) {
        if (!chain->entries[i]) continue;
        mctx->depth = i;
        MiddlewareResult result = chain->entries[i](req, res, mctx);
        if (result == MIDDLEWARE_STOP)  return true;
        if (result == MIDDLEWARE_ERROR) return false;
    }
    return true;
}

void middleware_chain_clear(MiddlewareChain *chain) {
    memset(chain, 0, sizeof(*chain));
}

/* ── Logger ─────────────────────────────────────────────────────────────── */
MiddlewareResult middleware_logger(HttpRequest *req, HttpResponse *res,
                                    MiddlewareContext *ctx) {
    (void)res;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    const char *ip = http_request_get_header(req, "X-Forwarded-For");
    if (!ip) ip = "127.0.0.1";

    snprintf(ctx->log_buf, MIDDLEWARE_MAX_LOG_LINE,
             "[%s] %s %s %s",
             time_buf, ip, http_method_str(req->method), req->path);
    return MIDDLEWARE_NEXT;
}

/* ── CORS ───────────────────────────────────────────────────────────────── */
MiddlewareResult middleware_cors(HttpRequest *req, HttpResponse *res,
                                  MiddlewareContext *ctx) {
    (void)ctx;
    http_response_add_header(res, "Access-Control-Allow-Origin",  g_cors_origin);
    http_response_add_header(res, "Access-Control-Allow-Methods", g_cors_methods);
    http_response_add_header(res, "Access-Control-Allow-Headers", g_cors_headers);
    http_response_add_header(res, "Vary", "Origin");

    if (req->method == HTTP_OPTIONS) {
        http_response_set_status(res, HTTP_STATUS_NO_CONTENT);
        return MIDDLEWARE_STOP;
    }
    return MIDDLEWARE_NEXT;
}

void middleware_cors_set_origin(const char *origin) {
    strncpy(g_cors_origin, origin, sizeof(g_cors_origin) - 1);
}

void middleware_cors_set_methods(const char *methods) {
    strncpy(g_cors_methods, methods, sizeof(g_cors_methods) - 1);
}

void middleware_cors_set_headers(const char *headers) {
    strncpy(g_cors_headers, headers, sizeof(g_cors_headers) - 1);
}

/* ── Gzip Compression (simulated) ─────────────────────────────────────────── */
MiddlewareResult middleware_compress_gzip(HttpRequest *req, HttpResponse *res,
                                           MiddlewareContext *ctx) {
    (void)ctx;
    const char *accept = http_request_get_header(req, "Accept-Encoding");
    if (accept && strstr(accept, "gzip")) {
        http_response_add_header(res, "Content-Encoding", "gzip");
        http_response_add_header(res, "Vary", "Accept-Encoding");
    }
    return MIDDLEWARE_NEXT;
}

/* ── Bearer Auth ────────────────────────────────────────────────────────── */
MiddlewareResult middleware_auth_bearer(HttpRequest *req, HttpResponse *res,
                                         MiddlewareContext *ctx) {
    (void)ctx;
    const char *auth = http_request_get_header(req, "Authorization");
    if (!auth || strncmp(auth, "Bearer ", 7) != 0) {
        http_response_set_status(res, HTTP_STATUS_UNAUTHORIZED);
        http_response_add_header(res, "WWW-Authenticate", "Bearer");
        http_response_set_body_str(res, "{\"error\":\"unauthorized\"}");
        return MIDDLEWARE_STOP;
    }
    return MIDDLEWARE_NEXT;
}

/* ── Rate Limit ─────────────────────────────────────────────────────────── */
static int   g_rate_limit_count = 0;
static time_t g_rate_window_start = 0;

MiddlewareResult middleware_rate_limit(HttpRequest *req, HttpResponse *res,
                                        MiddlewareContext *ctx) {
    (void)req;
    (void)ctx;
    time_t now = time(NULL);
    if (now - g_rate_window_start > 60) {
        g_rate_window_start = now;
        g_rate_limit_count = 0;
    }
    g_rate_limit_count++;
    if (g_rate_limit_count > 100) {
        http_response_set_status(res, HTTP_STATUS_TOO_MANY_REQUESTS);
        http_response_add_header(res, "Retry-After", "60");
        http_response_set_body_str(res, "{\"error\":\"rate limit exceeded\"}");
        return MIDDLEWARE_STOP;
    }
    http_response_add_header(res, "X-RateLimit-Remaining",
                              "100");
    return MIDDLEWARE_NEXT;
}

/* ── Body Parser ────────────────────────────────────────────────────────── */
MiddlewareResult middleware_body_parser(HttpRequest *req, HttpResponse *res,
                                         MiddlewareContext *ctx) {
    (void)ctx;
    (void)res;
    const char *len_str = http_request_get_header(req, "Content-Length");
    if (!len_str) return MIDDLEWARE_NEXT;

    long clen = strtol(len_str, NULL, 10);
    if (clen <= 0 || clen > HTTP_MAX_BODY) return MIDDLEWARE_ERROR;

    const char *ctype = http_request_get_header(req, "Content-Type");
    if (ctype && strstr(ctype, "application/json")) {
        http_response_add_header(res, "X-Body-Parsed", "json");
    }
    return MIDDLEWARE_NEXT;
}
