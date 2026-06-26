#ifndef MIDDLEWARE_H
#define MIDDLEWARE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * L1: Core Definitions — Middleware Pipeline
 *
 * Implements the Chain of Responsibility design pattern (GoF, 1994).
 * Each middleware handler can inspect, modify, or short-circuit the
 * request/response flow. This is the backbone of Express.js, ASP.NET
 * Core, and Django middleware stacks.
 *
 * L3: Engineering Structure — doubly-linked handler chain with
 * bidirectional traversal for request (forward) and response (reverse).
 *
 * L4: Theorem — Pipeline correctness requires that short-circuiting
 * preserves the invariant: response handlers for previously executed
 * request handlers are always invoked (stack-like LIFO order).
 */

#define MW_MAX_HANDLERS     32
#define MW_MAX_NAME         64
#define MW_MAX_CONTEXT_KEY  64
#define MW_MAX_CONTEXT_VAL  256

typedef enum {
    MW_GET,
    MW_POST,
    MW_PUT,
    MW_DELETE,
    MW_PATCH
} MWMethod;

typedef struct {
    char key[MW_MAX_CONTEXT_KEY];
    char value[MW_MAX_CONTEXT_VAL];
} MWKeyValue;

typedef struct {
    MWMethod   method;
    char       path[256];
    MWKeyValue headers[16];
    int        header_count;
    MWKeyValue params[16];
    int        param_count;
    char       body[4096];
    int        body_len;
    MWKeyValue context[16];
    int        ctx_count;
    bool       aborted;
    int        status_code;
    char       response_body[4096];
    int        response_len;
} MWRequest;

typedef int (*MWHandler)(MWRequest *req);

typedef struct MWNoded {
    MWHandler       handler;
    char            name[MW_MAX_NAME];
    struct MWNoded *next;
    struct MWNoded *prev;
} MWNode;

typedef struct {
    MWNode  *head;
    MWNode  *tail;
    int      count;
    MWHandler error_handler;
} MWPipeline;

void mw_pipeline_init(MWPipeline *pipe);
int  mw_use(MWPipeline *pipe, const char *name, MWHandler handler);
void mw_on_error(MWPipeline *pipe, MWHandler handler);
int  mw_process(MWPipeline *pipe, MWRequest *req);
int  mw_remove(MWPipeline *pipe, const char *name);
int  mw_insert_at(MWPipeline *pipe, int position, const char *name, MWHandler handler);
int  mw_count(const MWPipeline *pipe);
void mw_pipeline_destroy(MWPipeline *pipe);

void mw_request_init(MWRequest *req, MWMethod method, const char *path);
void mw_req_add_header(MWRequest *req, const char *key, const char *value);
void mw_req_add_param(MWRequest *req, const char *key, const char *value);
void mw_req_set_ctx(MWRequest *req, const char *key, const char *value);
const char *mw_req_get_ctx(const MWRequest *req, const char *key);
const char *mw_req_get_header(const MWRequest *req, const char *key);
void mw_req_respond(MWRequest *req, const char *data);
void mw_req_status(MWRequest *req, int code);
const char *mw_method_string(MWMethod method);

#endif
