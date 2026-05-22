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

typedef struct {
    char    uri[REST_MAX_URI_LEN];
    char    base_path[512];
    int32_t version;
} rest_router_t;

const char* rest_method_string(rest_method_t method);
const char* rest_status_string(rest_status_t status);
const char* rest_status_reason(rest_status_t status);

void rest_resource_init(rest_resource_t* r, const char* name, const char* path, rest_method_t method);
void rest_resource_set_status(rest_resource_t* r, rest_status_t status);
void rest_resource_set_body(rest_resource_t* r, const char* body);
void rest_resource_set_content_type(rest_resource_t* r, const char* ct);
void rest_resource_add_link(rest_resource_t* r, const char* rel, const char* href, const char* method);
void rest_resource_add_header(rest_resource_t* r, const char* key, const char* value);
void rest_resource_add_path_param(rest_resource_t* r, const char* name, const char* value);
void rest_pagination_init(rest_pagination_t* p, int32_t page, int32_t page_size, int32_t total);

void rest_router_init(rest_router_t* r, const char* base_path, int32_t version);
void rest_router_register(rest_router_t* r, rest_resource_t* resource);
const char* rest_router_resolve(rest_router_t* r, const char* uri, rest_method_t method);

#endif
