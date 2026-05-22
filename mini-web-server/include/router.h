#ifndef ROUTER_H
#define ROUTER_H

#include <stdbool.h>
#include <stddef.h>
#include "http_core.h"

/* ── Max Constants ──────────────────────────────────────────────────────── */
#define ROUTE_MAX_CHILDREN     64
#define ROUTE_MAX_PARAMS       16
#define ROUTE_MAX_PARAM_NAME   64
#define ROUTE_MAX_PARAM_VALUE  256

/* ── Route Parameter ───────────────────────────────────────────────────── */
typedef struct {
    char name[ROUTE_MAX_PARAM_NAME];
    char value[ROUTE_MAX_PARAM_VALUE];
} RouteParam;

/* ── Forward Declarations ──────────────────────────────────────────────── */
typedef struct RouteNode RouteNode;
typedef struct Router Router;

/* ── Route Handler Signature ───────────────────────────────────────────── */
typedef bool (*RouteHandler)(const HttpRequest *req, HttpResponse *res,
                              const RouteParam *params, int param_count);

/* ── Trie Node ──────────────────────────────────────────────────────────── */
struct RouteNode {
    char segment[256];
    RouteNode *children[ROUTE_MAX_CHILDREN];
    int child_count;

    bool is_endpoint;
    HttpMethod method;
    RouteHandler handler;

    bool is_wildcard;
    bool is_param;
    char param_name[ROUTE_MAX_PARAM_NAME];
};

/* ── Router ─────────────────────────────────────────────────────────────── */
struct Router {
    RouteNode *root;
};

/* ── Function Declarations ──────────────────────────────────────────────── */
Router     *router_create(void);
void        router_destroy(Router *router);
bool        router_add(Router *router, HttpMethod method, const char *pattern,
                       RouteHandler handler);
bool        router_dispatch(const Router *router, HttpMethod method,
                            const char *path, const HttpRequest *req,
                            HttpResponse *res);
RouteNode  *route_node_create(const char *segment);
void        route_node_destroy(RouteNode *node);

#endif /* ROUTER_H */
