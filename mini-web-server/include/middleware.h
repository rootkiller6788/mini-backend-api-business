#ifndef MIDDLEWARE_H
#define MIDDLEWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "http_core.h"

/* ── Max Constants ──────────────────────────────────────────────────────── */
#define MIDDLEWARE_MAX_CHAIN     16
#define MIDDLEWARE_MAX_LOG_LINE 1024

/* ── Middleware Result ──────────────────────────────────────────────────── */
typedef enum {
    MIDDLEWARE_NEXT,
    MIDDLEWARE_STOP,
    MIDDLEWARE_ERROR
} MiddlewareResult;

/* ── Middleware Context ─────────────────────────────────────────────────── */
typedef struct MiddlewareContext MiddlewareContext;

typedef MiddlewareResult (*MiddlewareFn)(HttpRequest *req, HttpResponse *res,
                                          MiddlewareContext *ctx);

/* ── Middleware Context ─────────────────────────────────────────────────── */
struct MiddlewareContext {
    void *user_data;
    int  depth;
    char log_buf[MIDDLEWARE_MAX_LOG_LINE];
};

/* ── Middleware Chain ───────────────────────────────────────────────────── */
typedef struct {
    MiddlewareFn entries[MIDDLEWARE_MAX_CHAIN];
    void        *contexts[MIDDLEWARE_MAX_CHAIN];
    int          count;
} MiddlewareChain;

/* ── Function Declarations ──────────────────────────────────────────────── */
void  middleware_chain_init(MiddlewareChain *chain);
bool  middleware_chain_add(MiddlewareChain *chain, MiddlewareFn fn,
                            void *user_ctx);
bool  middleware_chain_execute(MiddlewareChain *chain, HttpRequest *req,
                                HttpResponse *res, MiddlewareContext *mctx);
void  middleware_chain_clear(MiddlewareChain *chain);

/* ── Built-in Middleware ────────────────────────────────────────────────── */
MiddlewareResult middleware_logger(HttpRequest *req, HttpResponse *res,
                                    MiddlewareContext *ctx);
MiddlewareResult middleware_cors(HttpRequest *req, HttpResponse *res,
                                  MiddlewareContext *ctx);
MiddlewareResult middleware_compress_gzip(HttpRequest *req, HttpResponse *res,
                                           MiddlewareContext *ctx);
MiddlewareResult middleware_auth_bearer(HttpRequest *req, HttpResponse *res,
                                         MiddlewareContext *ctx);
MiddlewareResult middleware_rate_limit(HttpRequest *req, HttpResponse *res,
                                        MiddlewareContext *ctx);
MiddlewareResult middleware_body_parser(HttpRequest *req, HttpResponse *res,
                                         MiddlewareContext *ctx);

/* ── CORS configuration helpers ────────────────────────────────────────── */
void middleware_cors_set_origin(const char *origin);
void middleware_cors_set_methods(const char *methods);
void middleware_cors_set_headers(const char *headers);

#endif /* MIDDLEWARE_H */
