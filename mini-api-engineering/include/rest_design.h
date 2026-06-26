#ifndef REST_DESIGN_H
#define REST_DESIGN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define REST_MAX_URI_LEN     2048
#define REST_MAX_METHOD_LEN  16
#define REST_MAX_LINKS       32
#define REST_MAX_HEADERS     32
#define REST_MAX_PATH_PARAMS 16
#define REST_PAGE_DEFAULT    1
#define REST_PAGE_SIZE_MAX   100
#define REST_MAX_ROUTES      128
#define REST_MAX_MEDIA_TYPES 16
#define REST_MAX_CORS_ORIGINS 16
#define REST_ETAG_LEN        64
#define REST_CACHE_MAX_AGE   86400

typedef enum {
    REST_GET,
    REST_POST,
    REST_PUT,
    REST_PATCH,
    REST_DELETE,
    REST_HEAD,
    REST_OPTIONS,
    REST_METHOD_COUNT
} rest_method_t;

typedef enum {
    REST_200_OK                    = 200,
    REST_201_CREATED               = 201,
    REST_202_ACCEPTED              = 202,
    REST_204_NO_CONTENT            = 204,
    REST_301_MOVED_PERMANENTLY     = 301,
    REST_302_FOUND                 = 302,
    REST_304_NOT_MODIFIED          = 304,
    REST_400_BAD_REQUEST           = 400,
    REST_401_UNAUTHORIZED          = 401,
    REST_403_FORBIDDEN             = 403,
    REST_404_NOT_FOUND             = 404,
    REST_405_METHOD_NOT_ALLOWED    = 405,
    REST_409_CONFLICT              = 409,
    REST_410_GONE                  = 410,
    REST_415_UNSUPPORTED_MEDIA     = 415,
    REST_422_UNPROCESSABLE_ENTITY  = 422,
    REST_429_TOO_MANY_REQUESTS     = 429,
    REST_500_INTERNAL_SERVER_ERROR = 500,
    REST_502_BAD_GATEWAY           = 502,
    REST_503_SERVICE_UNAVAILABLE   = 503,
    REST_504_GATEWAY_TIMEOUT       = 504
} rest_status_t;

typedef struct {
    char rel[64];
    char href[REST_MAX_URI_LEN];
    char method[REST_MAX_METHOD_LEN];
} rest_link_t;

typedef struct {
    int32_t page;
    int32_t page_size;
    int32_t total_items;
    int32_t total_pages;
    bool    has_next;
    bool    has_prev;
    char    next_link[REST_MAX_URI_LEN];
    char    prev_link[REST_MAX_URI_LEN];
    char    first_link[REST_MAX_URI_LEN];
    char    last_link[REST_MAX_URI_LEN];
} rest_pagination_t;

typedef struct {
    char key[128];
    char value[512];
} rest_header_t;

typedef struct {
    char name[64];
    char value[256];
} rest_path_param_t;

typedef struct {
    char        name[256];
    char        path[REST_MAX_URI_LEN];
    rest_method_t method;
    rest_status_t status;
    char        content_type[128];
    char        response_body[8192];
    int32_t     response_len;
    rest_link_t links[REST_MAX_LINKS];
    int32_t     link_count;
    rest_header_t headers[REST_MAX_HEADERS];
    int32_t     header_count;
    rest_path_param_t params[REST_MAX_PATH_PARAMS];
    int32_t     param_count;
    rest_pagination_t pagination;
} rest_resource_t;

typedef enum {
    REST_MEDIA_JSON        = 0,
    REST_MEDIA_XML         = 1,
    REST_MEDIA_FORM        = 2,
    REST_MEDIA_MULTIPART   = 3,
    REST_MEDIA_TEXT_PLAIN  = 4,
    REST_MEDIA_TEXT_HTML   = 5,
    REST_MEDIA_OCTET_STREAM= 6,
    REST_MEDIA_PROTOBUF    = 7,
    REST_MEDIA_ANY         = 8
} rest_media_type_t;

typedef enum {
    REST_CACHE_NO_STORE      = 0,
    REST_CACHE_PRIVATE       = 1,
    REST_CACHE_PUBLIC        = 2,
    REST_CACHE_NO_CACHE      = 3
} rest_cache_control_t;

typedef struct {
    char origin[256];
    bool allow_credentials;
    char allowed_methods[128];
    char allowed_headers[512];
    int32_t max_age;
} rest_cors_policy_t;

typedef struct {
    char value[REST_ETAG_LEN];
    bool is_weak;
} rest_etag_t;

typedef struct {
    char type[64];
    char subtype[64];
    double quality;
    int32_t level;
} rest_media_range_t;

typedef struct {
    rest_resource_t* routes[REST_MAX_ROUTES];
    int32_t          route_count;
    char             uri[REST_MAX_URI_LEN];
    char             base_path[512];
    int32_t          version;
    double           rate_limit_tokens;
    double           rate_limit_last_refill;
    double           rate_limit_capacity;
    double           rate_limit_rate;
    rest_cors_policy_t cors;
} rest_router_t;

const char* rest_method_string(rest_method_t method);
const char* rest_status_string(rest_status_t status);
const char* rest_status_reason(rest_status_t status);
const char* rest_media_type_to_mime(rest_media_type_t t);

void rest_resource_init(rest_resource_t* r, const char* name, const char* path, rest_method_t method);
void rest_resource_set_status(rest_resource_t* r, rest_status_t status);
void rest_resource_set_body(rest_resource_t* r, const char* body);
void rest_resource_set_content_type(rest_resource_t* r, const char* ct);
void rest_resource_add_link(rest_resource_t* r, const char* rel, const char* href, const char* method);
void rest_resource_add_header(rest_resource_t* r, const char* key, const char* value);
void rest_resource_add_path_param(rest_resource_t* r, const char* name, const char* value);
void rest_pagination_init(rest_pagination_t* p, int32_t page, int32_t page_size, int32_t total);

void rest_router_init(rest_router_t* r, const char* base_path, int32_t version);
bool rest_router_register(rest_router_t* r, rest_resource_t* resource);
rest_resource_t* rest_router_resolve(rest_router_t* r, const char* uri, rest_method_t method);
void rest_router_set_cors(rest_router_t* r, const char* origin, bool credentials,
                           const char* methods, const char* headers, int32_t max_age);
void rest_router_set_rate_limit(rest_router_t* r, double rate, double capacity);
bool rest_router_check_rate_limit(rest_router_t* r);

rest_etag_t rest_etag_strong(const char* body, int32_t len);
rest_etag_t rest_etag_weak(const char* body, int32_t len);
bool rest_etag_match(rest_etag_t server_etag, const char* if_none_match);
int  rest_etag_compare(rest_etag_t a, rest_etag_t b);

rest_media_type_t rest_negotiate_content_type(const char* accept_header,
                                               const rest_media_type_t* supported, int32_t count);
rest_media_range_t rest_parse_media_range(const char* accept_entry);

char* rest_build_cors_headers(rest_cors_policy_t* policy, const char* origin,
                               char* buf, size_t len);
char* rest_build_cache_headers(rest_etag_t etag, int32_t max_age, rest_cache_control_t cc,
                                char* buf, size_t len);

bool rest_is_safe_method(rest_method_t m);
bool rest_is_idempotent_method(rest_method_t m);
bool rest_is_status_success(rest_status_t s);
bool rest_is_status_client_error(rest_status_t s);
bool rest_is_status_server_error(rest_status_t s);

#endif
