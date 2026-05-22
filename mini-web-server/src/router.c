#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RouteNode *route_node_create(const char *segment) {
    RouteNode *node = calloc(1, sizeof(RouteNode));
    if (!node) return NULL;
    strncpy(node->segment, segment, sizeof(node->segment) - 1);

    if (segment[0] == ':') {
        node->is_param = true;
        strncpy(node->param_name, segment + 1, ROUTE_MAX_PARAM_NAME - 1);
    } else if (strcmp(segment, "*") == 0) {
        node->is_wildcard = true;
    }
    return node;
}

void route_node_destroy(RouteNode *node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        route_node_destroy(node->children[i]);
    }
    free(node);
}

Router *router_create(void) {
    Router *r = calloc(1, sizeof(Router));
    if (!r) return NULL;
    r->root = route_node_create("/");
    if (!r->root) { free(r); return NULL; }
    return r;
}

void router_destroy(Router *router) {
    if (!router) return;
    route_node_destroy(router->root);
    free(router);
}

static char **split_path(const char *pattern, int *out_count) {
    int cap = 16, count = 0;
    char **segs = malloc((size_t)cap * sizeof(char *));
    if (!segs) return NULL;

    char *dup = strdup(pattern);
    if (!dup) { free(segs); return NULL; }

    char *save = NULL;
    char *tok = strtok_r(dup, "/", &save);
    while (tok) {
        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(segs, (size_t)cap * sizeof(char *));
            if (!tmp) { free(dup); free(segs); return NULL; }
            segs = tmp;
        }
        segs[count++] = strdup(tok);
        tok = strtok_r(NULL, "/", &save);
    }
    free(dup);
    *out_count = count;
    return segs;
}

bool router_add(Router *router, HttpMethod method, const char *pattern,
                RouteHandler handler) {
    if (!router || !pattern || !handler) return false;

    int seg_count = 0;
    char **segs = split_path(pattern, &seg_count);
    if (!segs) return false;

    RouteNode *cur = router->root;
    for (int i = 0; i < seg_count; i++) {
        bool found = false;
        for (int j = 0; j < cur->child_count; j++) {
            if (strcmp(cur->children[j]->segment, segs[i]) == 0) {
                cur = cur->children[j];
                found = true;
                break;
            }
        }
        if (!found) {
            if (cur->child_count >= ROUTE_MAX_CHILDREN) {
                for (int k = 0; k < seg_count; k++) free(segs[k]);
                free(segs);
                return false;
            }
            RouteNode *child = route_node_create(segs[i]);
            if (!child) {
                for (int k = 0; k < seg_count; k++) free(segs[k]);
                free(segs);
                return false;
            }
            cur->children[cur->child_count++] = child;
            cur = child;
        }
    }

    cur->is_endpoint = true;
    cur->method = method;
    cur->handler = handler;

    for (int k = 0; k < seg_count; k++) free(segs[k]);
    free(segs);
    return true;
}

static bool match_node(const RouteNode *node, const char *segment,
                        RouteParam *params, int *param_count) {
    if (!node->is_param && !node->is_wildcard)
        return strcmp(node->segment, segment) == 0;

    if (node->is_param && *param_count < ROUTE_MAX_PARAMS) {
        strncpy(params[*param_count].name, node->param_name,
                ROUTE_MAX_PARAM_NAME - 1);
        strncpy(params[*param_count].value, segment,
                ROUTE_MAX_PARAM_VALUE - 1);
        (*param_count)++;
        return true;
    }

    if (node->is_wildcard) {
        strncpy(params[*param_count].name, "wildcard",
                ROUTE_MAX_PARAM_NAME - 1);
        strncpy(params[*param_count].value, segment,
                ROUTE_MAX_PARAM_VALUE - 1);
        (*param_count)++;
        return true;
    }
    return false;
}

bool router_dispatch(const Router *router, HttpMethod method,
                      const char *path, const HttpRequest *req,
                      HttpResponse *res) {
    if (!router || !path) return false;

    int seg_count = 0;
    char **segs = split_path(path, &seg_count);
    if (!segs) return false;

    RouteParam params[ROUTE_MAX_PARAMS];
    int param_count = 0;
    RouteNode *cur = router->root;

    for (int i = 0; i < seg_count && cur; i++) {
        bool matched = false;
        for (int j = 0; j < cur->child_count; j++) {
            if (match_node(cur->children[j], segs[i], params, &param_count)) {
                cur = cur->children[j];
                matched = true;
                break;
            }
        }
        if (!matched) {
            for (int k = 0; k < seg_count; k++) free(segs[k]);
            free(segs);
            return false;
        }
    }

    bool handled = false;
    if (cur && cur->is_endpoint && cur->method == method && cur->handler) {
        handled = cur->handler(req, res, params, param_count);
    }

    for (int k = 0; k < seg_count; k++) free(segs[k]);
    free(segs);
    return handled;
}
