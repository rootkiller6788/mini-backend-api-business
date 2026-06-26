#ifndef GATEWAY_ROUTING_H
#define GATEWAY_ROUTING_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GW_MAX_PATH_LEN       256
#define GW_MAX_HOST_LEN       128
#define GW_MAX_SERVICE_LEN    64
#define GW_MAX_UPSTREAM_LEN   512
#define GW_MAX_RULES          1024
#define GW_MAX_HEADERS        32
#define GW_MAX_BODY_LEN       65536
#define GW_DEFAULT_RATE_LIMIT 1000

typedef enum {
    GW_METHOD_GET     = 0,
    GW_METHOD_POST    = 1,
    GW_METHOD_PUT     = 2,
    GW_METHOD_DELETE  = 3,
    GW_METHOD_ANY     = 4
} gw_http_method_t;

typedef enum {
    GW_LB_ROUND_ROBIN  = 0,
    GW_LB_WEIGHTED     = 1,
    GW_LB_LEAST_CONN   = 2
} gw_lb_algorithm_t;

typedef struct {
    char            key[GW_MAX_PATH_LEN];
    char            value[GW_MAX_PATH_LEN];
} gw_header_t;

typedef struct {
    gw_http_method_t method;
    char             path[GW_MAX_PATH_LEN];
    char             service_name[GW_MAX_SERVICE_LEN];
    char             upstream_url[GW_MAX_UPSTREAM_LEN];
    int              strip_prefix;
    int              auth_required;
    int              rate_limit_per_sec;
    int32_t          priority;
    int              enabled;
    int              timeout_ms;
} gw_route_rule_t;

typedef struct {
    char             host[GW_MAX_HOST_LEN];
    uint16_t         port;
    int              weight;
    int              max_connections;
    int              active_connections;
    int              healthy;
} gw_upstream_t;

typedef struct {
    gw_http_method_t method;
    char             path[GW_MAX_PATH_LEN];
    gw_header_t      headers[GW_MAX_HEADERS];
    int              header_count;
    uint8_t         *body;
    size_t           body_len;
    char             client_ip[46];
} gw_request_t;

typedef struct {
    int              status_code;
    gw_header_t      headers[GW_MAX_HEADERS];
    int              header_count;
    uint8_t         *body;
    size_t           body_len;
} gw_response_t;

typedef int  (*gw_auth_check_fn)(gw_request_t *req, void *user_data);
typedef void (*gw_transform_req_fn)(gw_request_t *req, void *user_data);
typedef void (*gw_transform_resp_fn)(gw_response_t *resp, void *user_data);

typedef struct gw_gateway gw_gateway_t;

gw_gateway_t *gw_gateway_create(void);
void          gw_gateway_destroy(gw_gateway_t *gateway);

int           gw_route_add(gw_gateway_t *gateway, const gw_route_rule_t *rule);
int           gw_route_remove(gw_gateway_t *gateway, gw_http_method_t method, const char *path);
int           gw_route_update(gw_gateway_t *gateway, const gw_route_rule_t *rule);
gw_route_rule_t *gw_route_find(gw_gateway_t *gateway, gw_http_method_t method, const char *path);
int           gw_route_list(gw_gateway_t *gateway, gw_route_rule_t *rules, int *count);

int           gw_upstream_add(gw_gateway_t *gateway, const char *service_name,
                              const gw_upstream_t *upstream);
int           gw_upstream_remove(gw_gateway_t *gateway, const char *service_name,
                                 const char *host, uint16_t port);
gw_upstream_t *gw_upstream_select(gw_gateway_t *gateway, const char *service_name,
                                  gw_lb_algorithm_t algorithm);

int           gw_forward(gw_gateway_t *gateway, gw_request_t *req, gw_response_t *resp);
int           gw_aggregate(gw_gateway_t *gateway, gw_request_t **reqs, int req_count,
                           gw_response_t *aggregated);

int           gw_set_auth_check(gw_gateway_t *gateway, gw_auth_check_fn fn, void *user_data);
int           gw_set_req_transform(gw_gateway_t *gateway, gw_transform_req_fn fn, void *user_data);
int           gw_set_resp_transform(gw_gateway_t *gateway, gw_transform_resp_fn fn, void *user_data);

int           gw_rate_limiter_allow(gw_gateway_t *gateway, const char *path, const char *client_ip);
int           gw_rate_limiter_reset(gw_gateway_t *gateway, const char *path);

void          gw_request_init(gw_request_t *req);
void          gw_request_free(gw_request_t *req);
void          gw_response_init(gw_response_t *resp);
void          gw_response_free(gw_response_t *resp);

/* === L5: Exponential Backoff with Jitter === */
typedef enum {
    GW_RETRY_JITTER_NONE         = 0,
    GW_RETRY_JITTER_FULL         = 1,
    GW_RETRY_JITTER_DECORRELATED = 2
} gw_jitter_strategy_t;

typedef struct {
    int                   max_retries;
    int32_t               base_delay_ms;
    int32_t               max_delay_ms;
    gw_jitter_strategy_t  jitter_strategy;
    int                   retry_on_5xx;
    int                   retry_on_timeout;
} gw_retry_policy_t;

int           gw_forward_with_retry(gw_gateway_t *gateway, gw_request_t *req,
                                     gw_response_t *resp, const gw_retry_policy_t *policy);

/* L7: Circuit Breaker integration (requires circuit_breaker.h) */
struct cb_circuit_breaker;
int           gw_forward_with_circuit_breaker(gw_gateway_t *gateway, gw_request_t *req,
                                               gw_response_t *resp,
                                               struct cb_circuit_breaker *cb);

/* L5: Smooth Weighted Round Robin */
gw_upstream_t *gw_upstream_select_swrr(gw_gateway_t *gateway,
                                        const char *service_name);

/* L7: Route observability */
typedef struct {
    char    path[GW_MAX_PATH_LEN];
    int64_t request_count;
    int64_t error_count;
    int64_t total_latency_us;
    int64_t last_request_time;
} gw_route_stats_t;

void          gw_route_stats_record(gw_gateway_t *gateway, const char *path,
                                     int status_code, int64_t latency_us);
int           gw_route_stats_get(const char *path, gw_route_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
