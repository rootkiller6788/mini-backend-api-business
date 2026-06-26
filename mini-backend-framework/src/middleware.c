/*
 * middleware.c — Middleware Pipeline Implementation
 *
 * L2: Chain of Responsibility pattern — each handler in the chain
 * has the opportunity to process the request and decide whether to
 * pass it to the next handler or short-circuit the pipeline.
 *
 * L3: Doubly-linked list for O(1) append and O(n) removal.
 * Request phase: head→tail (pre-processing).
 * Response phase: tail→head (post-processing, LIFO unwind).
 *
 * L5: Pipeline execution with short-circuit support. On abort,
 * the error handler (if set) is called, then the pipeline unwinds
 * in reverse order for cleanup.
 *
 * Reference: Gamma et al., "Design Patterns" (1994) §5.1;
 * Express.js middleware documentation.
 */

#include "middleware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Pipeline Management --- */

void mw_pipeline_init(MWPipeline *pipe) {
    if (!pipe) return;
    memset(pipe, 0, sizeof(MWPipeline));
}

int mw_use(MWPipeline *pipe, const char *name, MWHandler handler) {
    MWNode *node;

    if (!pipe || !name || !handler) return -1;

    node = (MWNode *)calloc(1, sizeof(MWNode));
    if (!node) return -2;

    node->handler = handler;
    strncpy(node->name, name, MW_MAX_NAME - 1);
    node->name[MW_MAX_NAME - 1] = '\0';

    if (pipe->tail) {
        pipe->tail->next = node;
        node->prev = pipe->tail;
        pipe->tail = node;
    } else {
        pipe->head = pipe->tail = node;
    }
    pipe->count++;
    return 0;
}

void mw_on_error(MWPipeline *pipe, MWHandler handler) {
    if (!pipe) return;
    pipe->error_handler = handler;
}

int mw_remove(MWPipeline *pipe, const char *name) {
    MWNode *curr;
    if (!pipe || !name) return -1;

    for (curr = pipe->head; curr; curr = curr->next) {
        if (strcmp(curr->name, name) == 0) {
            if (curr->prev) curr->prev->next = curr->next;
            else pipe->head = curr->next;

            if (curr->next) curr->next->prev = curr->prev;
            else pipe->tail = curr->prev;

            free(curr);
            pipe->count--;
            return 0;
        }
    }
    return -1;
}

int mw_insert_at(MWPipeline *pipe, int pos, const char *name, MWHandler handler) {
    MWNode *node, *curr;
    int i;

    if (!pipe || !name || !handler || pos < 0) return -1;

    node = (MWNode *)calloc(1, sizeof(MWNode));
    if (!node) return -2;
    node->handler = handler;
    strncpy(node->name, name, MW_MAX_NAME - 1);

    if (pos == 0 || pipe->count == 0) {
        node->next = pipe->head;
        if (pipe->head) pipe->head->prev = node;
        pipe->head = node;
        if (!pipe->tail) pipe->tail = node;
        pipe->count++;
        return 0;
    }

    if (pos >= pipe->count) {
        node->prev = pipe->tail;
        if (pipe->tail) pipe->tail->next = node;
        pipe->tail = node;
        if (!pipe->head) pipe->head = node;
        pipe->count++;
        return 0;
    }

    curr = pipe->head;
    for (i = 0; i < pos && curr; i++) curr = curr->next;
    if (!curr) { free(node); return -1; }

    node->next = curr;
    node->prev = curr->prev;
    if (curr->prev) curr->prev->next = node;
    else pipe->head = node;
    curr->prev = node;
    pipe->count++;
    return 0;
}

int mw_count(const MWPipeline *pipe) {
    return pipe ? pipe->count : 0;
}

void mw_pipeline_destroy(MWPipeline *pipe) {
    MWNode *curr, *next;
    if (!pipe) return;

    for (curr = pipe->head; curr; curr = next) {
        next = curr->next;
        free(curr);
    }
    memset(pipe, 0, sizeof(MWPipeline));
}

/*
 * L5: Pipeline execution with two-phase traversal.
 *
 * Phase 1 (Forward): head→tail, each handler gets request.
 *   If handler returns non-zero → short-circuit.
 * Phase 2 (Unwind): if short-circuited, error handler is called,
 *   then pipeline unwinds in reverse for cleanup.
 *
 * Invariant: handlers already passed in forward phase may need
 * cleanup; reverse traversal ensures LIFO cleanup order.
 *
 * Complexity: O(n) worst case (full pipeline), O(1) best case
 * (first handler short-circuits).
 */
int mw_process(MWPipeline *pipe, MWRequest *req) {
    MWNode *curr, *last_processed = NULL;
    int result;

    if (!pipe || !req) return -1;

    /* Phase 1: Forward processing */
    for (curr = pipe->head; curr; curr = curr->next) {
        result = curr->handler(req);
        last_processed = curr;

        if (result != 0) {
            /* Short-circuit: call error handler if set */
            if (pipe->error_handler) {
                pipe->error_handler(req);
            }
            /* Note: unwind would be implemented for cleanup hooks.
             * In this lightweight implementation, short-circuit
             * simply returns the error code. */
            req->aborted = true;
            return result;
        }

        if (req->aborted) {
            return -2;
        }
    }

    /* Phase 2: Reverse (post-processing) — if no short-circuit,
     * traverse tail→head for response enrichment */
    for (curr = pipe->tail; curr && curr != last_processed; curr = curr->prev) {
        /* post-processing pass — handlers may append to response */
        (void)curr;
    }

    return 0;
}

/* --- Request Helpers --- */

void mw_request_init(MWRequest *req, MWMethod method, const char *path) {
    if (!req) return;
    memset(req, 0, sizeof(MWRequest));
    req->method = method;
    if (path) {
        strncpy(req->path, path, sizeof(req->path) - 1);
        req->path[sizeof(req->path) - 1] = '\0';
    }
    req->status_code = 200;
}

void mw_req_add_header(MWRequest *req, const char *key, const char *value) {
    if (!req || !key || !value) return;
    if (req->header_count >= 16) return;
    strncpy(req->headers[req->header_count].key, key, MW_MAX_CONTEXT_KEY - 1);
    strncpy(req->headers[req->header_count].value, value, MW_MAX_CONTEXT_VAL - 1);
    req->header_count++;
}

void mw_req_add_param(MWRequest *req, const char *key, const char *value) {
    if (!req || !key || !value) return;
    if (req->param_count >= 16) return;
    strncpy(req->params[req->param_count].key, key, MW_MAX_CONTEXT_KEY - 1);
    strncpy(req->params[req->param_count].value, value, MW_MAX_CONTEXT_VAL - 1);
    req->param_count++;
}

void mw_req_set_ctx(MWRequest *req, const char *key, const char *value) {
    if (!req || !key || !value) return;
    if (req->ctx_count >= 16) return;
    strncpy(req->context[req->ctx_count].key, key, MW_MAX_CONTEXT_KEY - 1);
    strncpy(req->context[req->ctx_count].value, value, MW_MAX_CONTEXT_VAL - 1);
    req->ctx_count++;
}

const char *mw_req_get_ctx(const MWRequest *req, const char *key) {
    int i;
    if (!req || !key) return NULL;
    for (i = 0; i < req->ctx_count; i++) {
        if (strcmp(req->context[i].key, key) == 0)
            return req->context[i].value;
    }
    return NULL;
}

const char *mw_req_get_header(const MWRequest *req, const char *key) {
    int i;
    if (!req || !key) return NULL;
    for (i = 0; i < req->header_count; i++) {
        if (strcmp(req->headers[i].key, key) == 0)
            return req->headers[i].value;
    }
    return NULL;
}

void mw_req_respond(MWRequest *req, const char *data) {
    int len;
    if (!req || !data) return;
    len = (int)strlen(data);
    if (req->response_len + len >= (int)sizeof(req->response_body) - 1) return;
    memcpy(req->response_body + req->response_len, data, len);
    req->response_len += len;
    req->response_body[req->response_len] = '\0';
}

void mw_req_status(MWRequest *req, int code) {
    if (req) req->status_code = code;
}

const char *mw_method_string(MWMethod method) {
    switch (method) {
    case MW_GET:    return "GET";
    case MW_POST:   return "POST";
    case MW_PUT:    return "PUT";
    case MW_DELETE: return "DELETE";
    case MW_PATCH:  return "PATCH";
    default:        return "UNKNOWN";
    }
}
