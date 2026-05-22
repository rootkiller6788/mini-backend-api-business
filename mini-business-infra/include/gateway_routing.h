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

#ifdef __cplusplus
}
#endif

#endif
