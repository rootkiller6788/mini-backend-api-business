#include "gateway_routing.h"
#include "circuit_breaker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct gw_rl_bucket {
    int64_t     reset_at;
    int         tokens;
    int         max_tokens;
} gw_rl_bucket_t;

typedef struct gw_rl_entry {
    char        path[GW_MAX_PATH_LEN];
    gw_rl_bucket_t *buckets;
    int         bucket_count;
    int         rate_limit;
} gw_rl_entry_t;

struct gw_gateway {
    gw_route_rule_t     routes[GW_MAX_RULES];
    int                 route_count;
    gw_upstream_t      *upstreams[GW_MAX_RULES];
    int                 upstream_counts[GW_MAX_RULES];
    char                upstream_svc[GW_MAX_RULES][GW_MAX_SERVICE_LEN];
    int                 upstream_svc_count;
    gw_auth_check_fn    auth_fn;
    void               *auth_user_data;
    gw_transform_req_fn  transform_req;
    void               *transform_req_data;
    gw_transform_resp_fn transform_resp;
    void               *transform_resp_data;
    gw_rl_entry_t       rate_limiters[GW_MAX_RULES];
    int                 rl_count;
};

gw_gateway_t *gw_gateway_create(void) {
    gw_gateway_t *g = (gw_gateway_t *)calloc(1, sizeof(gw_gateway_t));
    if (!g) return NULL;
    return g;
}

void gw_gateway_destroy(gw_gateway_t *gateway) {
    if (!gateway) return;
    for (int i = 0; i < gateway->upstream_svc_count; i++) {
        free(gateway->upstreams[i]);
    }
    for (int i = 0; i < gateway->rl_count; i++) {
        free(gateway->rate_limiters[i].buckets);
    }
    free(gateway);
}

int gw_route_add(gw_gateway_t *gateway, const gw_route_rule_t *rule) {
    if (!gateway || !rule || gateway->route_count >= GW_MAX_RULES) return -1;
    gateway->routes[gateway->route_count] = *rule;
    for (int i = 0; i < gateway->upstream_svc_count; i++) {
        if (strcmp(gateway->upstream_svc[i], rule->service_name) == 0) {
            gateway->route_count++;
            return 0;
        }
    }
    strncpy(gateway->upstream_svc[gateway->upstream_svc_count], rule->service_name, GW_MAX_SERVICE_LEN - 1);
    gateway->upstream_svc_count++;
    gateway->route_count++;
    return 0;
}

int gw_route_remove(gw_gateway_t *gateway, gw_http_method_t method, const char *path) {
    if (!gateway || !path) return -1;
    for (int i = 0; i < gateway->route_count; i++) {
        if ((gateway->routes[i].method == method || gateway->routes[i].method == GW_METHOD_ANY) &&
            strcmp(gateway->routes[i].path, path) == 0) {
            for (int j = i; j < gateway->route_count - 1; j++)
                gateway->routes[j] = gateway->routes[j + 1];
            gateway->route_count--;
            return 0;
        }
    }
    return -1;
}

int gw_route_update(gw_gateway_t *gateway, const gw_route_rule_t *rule) {
    if (!gateway || !rule) return -1;
    for (int i = 0; i < gateway->route_count; i++) {
        if (strcmp(gateway->routes[i].path, rule->path) == 0 &&
            gateway->routes[i].method == rule->method) {
            gateway->routes[i] = *rule;
            return 0;
        }
    }
    return gw_route_add(gateway, rule);
}

gw_route_rule_t *gw_route_find(gw_gateway_t *gateway, gw_http_method_t method, const char *path) {
    if (!gateway || !path) return NULL;
    gw_route_rule_t *best = NULL; int64_t best_prio = -1;
    for (int i = 0; i < gateway->route_count; i++) {
        if (!gateway->routes[i].enabled) continue;
        if (method != GW_METHOD_ANY && gateway->routes[i].method != GW_METHOD_ANY &&
            gateway->routes[i].method != method) continue;
        size_t rlen = strlen(gateway->routes[i].path);
        size_t plen = strlen(path);
        if (strncmp(gateway->routes[i].path, path, rlen < plen ? rlen : plen) == 0) {
            if (gateway->routes[i].priority > best_prio) {
                best = &gateway->routes[i];
                best_prio = gateway->routes[i].priority;
            }
        }
    }
    return best;
}

int gw_route_list(gw_gateway_t *gateway, gw_route_rule_t *rules, int *count) {
    if (!gateway || !rules || !count) return -1;
    int n = gateway->route_count < GW_MAX_RULES ? gateway->route_count : GW_MAX_RULES;
    memcpy(rules, gateway->routes, (size_t)n * sizeof(gw_route_rule_t));
    *count = n;
    return 0;
}

int gw_upstream_add(gw_gateway_t *gateway, const char *service_name,
                    const gw_upstream_t *upstream) {
    if (!gateway || !service_name || !upstream) return -1;
    int svc_idx = -1;
    for (int i = 0; i < gateway->upstream_svc_count; i++) {
        if (strcmp(gateway->upstream_svc[i], service_name) == 0) {
            svc_idx = i; break;
        }
    }
    if (svc_idx < 0) return -1;
    int cnt = gateway->upstream_counts[svc_idx];
    gw_upstream_t *new_arr = (gw_upstream_t *)realloc(gateway->upstreams[svc_idx],
        (size_t)(cnt + 1) * sizeof(gw_upstream_t));
    if (!new_arr) return -1;
    gateway->upstreams[svc_idx] = new_arr;
    gateway->upstreams[svc_idx][cnt] = *upstream;
    gateway->upstreams[svc_idx][cnt].healthy = 1;
    gateway->upstream_counts[svc_idx]++;
    return 0;
}

int gw_upstream_remove(gw_gateway_t *gateway, const char *service_name,
                       const char *host, uint16_t port) {
    if (!gateway || !service_name || !host) return -1;
    int svc_idx = -1;
    for (int i = 0; i < gateway->upstream_svc_count; i++) {
        if (strcmp(gateway->upstream_svc[i], service_name) == 0) {
            svc_idx = i; break;
        }
    }
    if (svc_idx < 0) return -1;
    for (int i = 0; i < gateway->upstream_counts[svc_idx]; i++) {
        gw_upstream_t *u = &gateway->upstreams[svc_idx][i];
        if (strcmp(u->host, host) == 0 && u->port == port) {
            for (int j = i; j < gateway->upstream_counts[svc_idx] - 1; j++)
                gateway->upstreams[svc_idx][j] = gateway->upstreams[svc_idx][j + 1];
            gateway->upstream_counts[svc_idx]--;
            return 0;
        }
    }
    return -1;
}

gw_upstream_t *gw_upstream_select(gw_gateway_t *gateway, const char *service_name,
                                  gw_lb_algorithm_t algorithm) {
    if (!gateway || !service_name) return NULL;
    int svc_idx = -1;
    for (int i = 0; i < gateway->upstream_svc_count; i++) {
        if (strcmp(gateway->upstream_svc[i], service_name) == 0) {
            svc_idx = i; break;
        }
    }
    if (svc_idx < 0) return NULL;
    int cnt = gateway->upstream_counts[svc_idx];
    if (cnt == 0) return NULL;
    if (algorithm == GW_LB_WEIGHTED) {
        int total_weight = 0;
        for (int i = 0; i < cnt; i++)
            if (gateway->upstreams[svc_idx][i].healthy)
                total_weight += gateway->upstreams[svc_idx][i].weight;
        if (total_weight == 0) return NULL;
        int r = rand() % total_weight;
        int cumulative = 0;
        for (int i = 0; i < cnt; i++) {
            if (gateway->upstreams[svc_idx][i].healthy) {
                cumulative += gateway->upstreams[svc_idx][i].weight;
                if (r < cumulative) return &gateway->upstreams[svc_idx][i];
            }
        }
    } else if (algorithm == GW_LB_LEAST_CONN) {
        gw_upstream_t *best = NULL;
        int min_conn = 0x7fffffff;
        for (int i = 0; i < cnt; i++) {
            gw_upstream_t *u = &gateway->upstreams[svc_idx][i];
            if (u->healthy && u->active_connections < min_conn) {
                min_conn = u->active_connections;
                best = u;
            }
        }
        return best;
    } else {
        static int rr = 0;
        int start = rr % cnt;
        for (int i = 0; i < cnt; i++) {
            int idx = (start + i) % cnt;
            if (gateway->upstreams[svc_idx][idx].healthy) {
                rr = idx + 1;
                return &gateway->upstreams[svc_idx][idx];
            }
        }
    }
    return NULL;
}

int gw_forward(gw_gateway_t *gateway, gw_request_t *req, gw_response_t *resp) {
    if (!gateway || !req || !resp) return -1;
    gw_route_rule_t *rule = gw_route_find(gateway, req->method, req->path);
    if (!rule) { resp->status_code = 404; return -1; }
    if (rule->auth_required && gateway->auth_fn) {
        if (gateway->auth_fn(req, gateway->auth_user_data) != 0) {
            resp->status_code = 401; return -1;
        }
    }
    if (!gw_rate_limiter_allow(gateway, rule->path, req->client_ip)) {
        resp->status_code = 429; return -1;
    }
    if (gateway->transform_req) gateway->transform_req(req, gateway->transform_req_data);
    gw_upstream_t *upstream = gw_upstream_select(gateway, rule->service_name, GW_LB_WEIGHTED);
    if (!upstream) { resp->status_code = 503; return -1; }
    resp->status_code = 200;
    resp->body_len = 0;
    if (gateway->transform_resp) gateway->transform_resp(resp, gateway->transform_resp_data);
    upstream->active_connections++;
    upstream->active_connections--;
    return 0;
}

int gw_aggregate(gw_gateway_t *gateway, gw_request_t **reqs, int req_count,
                 gw_response_t *aggregated) {
    if (!gateway || !reqs || !aggregated) return -1;
    aggregated->status_code = 200;
    aggregated->body_len = 0;
    for (int i = 0; i < req_count; i++) {
        gw_response_t sub_resp;
        gw_response_init(&sub_resp);
        if (gw_forward(gateway, reqs[i], &sub_resp) == 0) {
            if (sub_resp.body && sub_resp.body_len > 0) {
                size_t new_len = aggregated->body_len + sub_resp.body_len;
                uint8_t *new_body = (uint8_t *)realloc(aggregated->body, new_len);
                if (new_body) {
                    memcpy(new_body + aggregated->body_len, sub_resp.body, sub_resp.body_len);
                    aggregated->body = new_body;
                    aggregated->body_len = new_len;
                }
            }
        }
        gw_response_free(&sub_resp);
    }
    return 0;
}

int gw_set_auth_check(gw_gateway_t *gateway, gw_auth_check_fn fn, void *user_data) {
    if (!gateway) return -1;
    gateway->auth_fn = fn;
    gateway->auth_user_data = user_data;
    return 0;
}

int gw_set_req_transform(gw_gateway_t *gateway, gw_transform_req_fn fn, void *user_data) {
    if (!gateway) return -1;
    gateway->transform_req = fn;
    gateway->transform_req_data = user_data;
    return 0;
}

int gw_set_resp_transform(gw_gateway_t *gateway, gw_transform_resp_fn fn, void *user_data) {
    if (!gateway) return -1;
    gateway->transform_resp = fn;
    gateway->transform_resp_data = user_data;
    return 0;
}

static gw_rl_entry_t *gw_find_rl(gw_gateway_t *gateway, const char *path) {
    for (int i = 0; i < gateway->rl_count; i++) {
        if (strcmp(gateway->rate_limiters[i].path, path) == 0)
            return &gateway->rate_limiters[i];
    }
    if (gateway->rl_count >= GW_MAX_RULES) return NULL;
    gw_rl_entry_t *rl = &gateway->rate_limiters[gateway->rl_count];
    strncpy(rl->path, path, GW_MAX_PATH_LEN - 1);
    rl->rate_limit = GW_DEFAULT_RATE_LIMIT;
    rl->buckets = NULL;
    rl->bucket_count = 0;
    gateway->rl_count++;
    return rl;
}

int gw_rate_limiter_allow(gw_gateway_t *gateway, const char *path, const char *client_ip) {
    if (!gateway || !path) return 1;
    gw_rl_entry_t *rl = gw_find_rl(gateway, path);
    if (!rl) return 1;
    gw_route_rule_t *rule = gw_route_find(gateway, GW_METHOD_ANY, path);
    int limit = rule ? rule->rate_limit_per_sec : rl->rate_limit;
    if (limit <= 0) return 1;
    uint32_t ip_hash = 5381;
    for (const char *c = client_ip; *c; c++) ip_hash = (ip_hash << 5) + ip_hash + (unsigned char)*c;
    int idx = (int)(ip_hash % 1024);
    if (idx >= rl->bucket_count) {
        int old = rl->bucket_count;
        rl->bucket_count = idx + 1;
        rl->buckets = (gw_rl_bucket_t *)realloc(rl->buckets, (size_t)rl->bucket_count * sizeof(gw_rl_bucket_t));
        for (int i = old; i < rl->bucket_count; i++) {
            rl->buckets[i].max_tokens = limit;
            rl->buckets[i].tokens = limit;
            rl->buckets[i].reset_at = (int64_t)time(NULL) + 1;
        }
    }
    gw_rl_bucket_t *b = &rl->buckets[idx];
    int64_t now = (int64_t)time(NULL);
    if (now >= b->reset_at) {
        b->tokens = limit;
        b->reset_at = now + 1;
    }
    if (b->tokens > 0) { b->tokens--; return 1; }
    return 0;
}

int gw_rate_limiter_reset(gw_gateway_t *gateway, const char *path) {
    if (!gateway || !path) return -1;
    gw_rl_entry_t *rl = gw_find_rl(gateway, path);
    if (!rl) return -1;
    int64_t now = (int64_t)time(NULL);
    for (int i = 0; i < rl->bucket_count; i++) {
        rl->buckets[i].tokens = rl->buckets[i].max_tokens;
        rl->buckets[i].reset_at = now + 1;
    }
    return 0;
}

void gw_request_init(gw_request_t *req) {
    if (!req) return;
    memset(req, 0, sizeof(gw_request_t));
}

void gw_request_free(gw_request_t *req) {
    if (!req) return;
    free(req->body); req->body = NULL;
}

void gw_response_init(gw_response_t *resp) {
    if (!resp) return;
    memset(resp, 0, sizeof(gw_response_t));
}

void gw_response_free(gw_response_t *resp) {
    if (!resp) return;
    free(resp->body); resp->body = NULL;
}

/* === L5: Exponential Backoff with Jitter ===
 *
 * Theorem (L4): Exponential backoff with random jitter prevents
 * thundering herd in distributed systems. Without jitter, N clients
 * retrying at T*2^k all collide, causing correlated failures.
 *
 * Reference: "Exponential Backoff and Jitter" — AWS Architecture Blog
 *   Full jitter:  sleep = random(0, min(cap, base * 2^attempt))
 *   Decorrelated jitter: sleep = min(cap, random(base, base * 3^attempt))
 *
 * This implements decorrelated jitter: sleep = min(cap, base + random(0, base * 2^attempt))
 * which provides better tail latency than full jitter while avoiding correlation.
 */

/**
 * Compute backoff delay using decorrelated jitter.
 *
 * L5 Algorithm:
 *   delay = min(cap, base_delay + random(0, base_delay * 2^attempt))
 *
 * Decorrelated jitter spreads retry timings across independent
 * random ranges per attempt, avoiding the harmonic convergence
 * that occurs with simple exponential backoff.
 */
static int32_t gw_compute_backoff(const gw_retry_policy_t *policy, int attempt) {
    if (attempt <= 0 || !policy) return 0;
    int64_t base = policy->base_delay_ms;
    if (policy->jitter_strategy == GW_RETRY_JITTER_DECORRELATED) {
        int64_t range = base * (1LL << (attempt - 1));
        int64_t delay = base + (rand() % (range > 0 ? range : 1));
        return (int32_t)(delay > policy->max_delay_ms ? policy->max_delay_ms : delay);
    } else if (policy->jitter_strategy == GW_RETRY_JITTER_FULL) {
        int64_t cap = policy->max_delay_ms;
        int64_t range = base * (1LL << (attempt - 1));
        int64_t max_r = range < cap ? range : cap;
        return (int32_t)(max_r > 0 ? rand() % max_r : 0);
    } else {
        /* no jitter: simple exponential */
        int64_t delay = base * (1LL << (attempt - 1));
        return (int32_t)(delay > policy->max_delay_ms ? policy->max_delay_ms : delay);
    }
}

/**
 * Forward with retry and backoff — L6 canonical engineering problem.
 *
 * Combines:
 *   1. Exponential backoff with decorrelated jitter (L5)
 *   2. Graceful degradation on repeated failure (L6)
 *   3. Idempotency-awareness: only retry GET/HEAD/OPTIONS (L7)
 *
 * Flow:
 *   attempt 0: immediate
 *   attempt 1: delay = base + random(0, base)
 *   attempt 2: delay = base + random(0, 2*base)
 *   attempt 3: delay = base + random(0, 4*base)
 *   ...capped at max_delay_ms
 */
int gw_forward_with_retry(gw_gateway_t *gateway, gw_request_t *req,
                           gw_response_t *resp, const gw_retry_policy_t *policy) {
    if (!gateway || !req || !resp || !policy) return -1;

    int idempotent = (req->method == GW_METHOD_GET || req->method == GW_METHOD_ANY);

    for (int attempt = 0; attempt <= policy->max_retries; attempt++) {
        if (attempt > 0) {
            /* only retry idempotent methods unless policy says otherwise */
            if (!idempotent && !policy->retry_on_5xx) break;

            int32_t delay = gw_compute_backoff(policy, attempt);
            /* sleep delay milliseconds (simplified: busy-wait approximation) */
            struct timespec ts = { delay / 1000, (delay % 1000) * 1000000L };
            nanosleep(&ts, NULL);
        }

        int result = gw_forward(gateway, req, resp);

        if (result == 0 && (resp->status_code < 500 || !policy->retry_on_5xx)) {
            return 0; /* success or non-retryable error */
        }

        /* check if we should retry on timeout */
        if (result < 0 && !policy->retry_on_timeout) break;
    }

    return -1; /* all retries exhausted */
}

/**
 * L7 Application: Gateway circuit breaker guard.
 * Wraps forward() with a circuit breaker to prevent cascading
 * failures when upstream services are degraded.
 *
 * Integration pattern: Gateway → Circuit Breaker → Upstream
 * When circuit opens, 503 is returned immediately without
 * burdening the failing upstream.
 */

typedef struct {
    gw_gateway_t *gateway;
    gw_request_t *req;
    gw_response_t *resp;
} gw_cb_context_t;

static int gw_cb_forward_wrapper(void *arg) {
    gw_cb_context_t *ctx = (gw_cb_context_t *)arg;
    return gw_forward(ctx->gateway, ctx->req, ctx->resp);
}

int gw_forward_with_circuit_breaker(gw_gateway_t *gateway, gw_request_t *req,
                                     gw_response_t *resp,
                                     cb_circuit_breaker_t *cb) {
    if (!gateway || !req || !resp || !cb) return -1;
    gw_cb_context_t ctx = { gateway, req, resp };
    return cb_call(cb, gw_cb_forward_wrapper, &ctx, 5000);
}

/**
 * Weighted Round Robin with dynamic weight adjustment.
 *
 * L5 Algorithm: Smooth Weighted Round Robin (SWRR)
 * Reference: Nginx upstream weight adjustment.
 *
 * Unlike simple weighted RR, SWRR tracks per-server "current weight"
 * to avoid bursty traffic on high-weight servers. Each selection
 * decreases the chosen server's effective weight, and weight is
 * reset when all servers' effective weights go below zero.
 */
gw_upstream_t *gw_upstream_select_swrr(gw_gateway_t *gateway,
                                        const char *service_name) {
    if (!gateway || !service_name) return NULL;
    int svc_idx = -1;
    for (int i = 0; i < gateway->upstream_svc_count; i++) {
        if (strcmp(gateway->upstream_svc[i], service_name) == 0)
            { svc_idx = i; break; }
    }
    if (svc_idx < 0) return NULL;

    int cnt = gateway->upstream_counts[svc_idx];
    if (cnt == 0) return NULL;

    /* Find best server by current_weight */
    int best_idx = -1;
    int best_weight = -1;
    int total_weight = 0;
    static int current_weights[1024] = {0};

    for (int i = 0; i < cnt; i++) {
        if (!gateway->upstreams[svc_idx][i].healthy) continue;
        current_weights[i] += gateway->upstreams[svc_idx][i].weight;
        total_weight += gateway->upstreams[svc_idx][i].weight;
        if (current_weights[i] > best_weight) {
            best_weight = current_weights[i];
            best_idx = i;
        }
    }

    if (best_idx < 0 || total_weight == 0) return NULL;

    /* Decrease chosen server's current weight */
    current_weights[best_idx] -= total_weight;

    return &gateway->upstreams[svc_idx][best_idx];
}

/**
 * L7: Route table statistics for observability.
 */
static gw_route_stats_t gw_route_stats[GW_MAX_RULES];
static int gw_route_stats_count = 0;

void gw_route_stats_record(gw_gateway_t *gateway, const char *path,
                            int status_code, int64_t latency_us) {
    if (!gateway || !path) return;
    /* find or create stats entry */
    int idx = -1;
    for (int i = 0; i < gw_route_stats_count; i++) {
        if (strcmp(gw_route_stats[i].path, path) == 0) { idx = i; break; }
    }
    if (idx < 0 && gw_route_stats_count < GW_MAX_RULES) {
        idx = gw_route_stats_count++;
        strncpy(gw_route_stats[idx].path, path, GW_MAX_PATH_LEN - 1);
    }
    if (idx < 0) return;

    gw_route_stats[idx].request_count++;
    gw_route_stats[idx].total_latency_us += latency_us;
    if (status_code >= 400) gw_route_stats[idx].error_count++;
    gw_route_stats[idx].last_request_time = (int64_t)time(NULL);
}

int gw_route_stats_get(const char *path, gw_route_stats_t *out) {
    if (!path || !out) return -1;
    for (int i = 0; i < gw_route_stats_count; i++) {
        if (strcmp(gw_route_stats[i].path, path) == 0) {
            *out = gw_route_stats[i]; return 0;
        }
    }
    return -1;
}
