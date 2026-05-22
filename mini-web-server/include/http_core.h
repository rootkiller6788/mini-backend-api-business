#ifndef HTTP_CORE_H
#define HTTP_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── HTTP Methods ──────────────────────────────────────────────────────── */
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_PATCH,
    HTTP_UNKNOWN
} HttpMethod;

/* ── HTTP Status Codes ─────────────────────────────────────────────────── */
#define HTTP_STATUS_OK                    200
#define HTTP_STATUS_CREATED               201
#define HTTP_STATUS_NO_CONTENT            204
#define HTTP_STATUS_PARTIAL_CONTENT       206
#define HTTP_STATUS_MOVED_PERMANENTLY     301
#define HTTP_STATUS_FOUND                 302
#define HTTP_STATUS_NOT_MODIFIED          304
#define HTTP_STATUS_BAD_REQUEST           400
#define HTTP_STATUS_UNAUTHORIZED          401
#define HTTP_STATUS_FORBIDDEN             403
#define HTTP_STATUS_NOT_FOUND             404
#define HTTP_STATUS_METHOD_NOT_ALLOWED    405
#define HTTP_STATUS_REQUEST_TIMEOUT       408
#define HTTP_STATUS_CONFLICT              409
#define HTTP_STATUS_GONE                  410
#define HTTP_STATUS_PAYLOAD_TOO_LARGE     413
#define HTTP_STATUS_URI_TOO_LONG          414
#define HTTP_STATUS_UNSUPPORTED_MEDIA     415
#define HTTP_STATUS_RANGE_NOT_SATISFIABLE 416
#define HTTP_STATUS_TOO_MANY_REQUESTS     429
#define HTTP_STATUS_INTERNAL_ERROR        500
#define HTTP_STATUS_NOT_IMPLEMENTED       501
#define HTTP_STATUS_BAD_GATEWAY           502
#define HTTP_STATUS_SERVICE_UNAVAILABLE   503
#define HTTP_STATUS_GATEWAY_TIMEOUT       504

/* ── Max Sizes ──────────────────────────────────────────────────────────── */
#define HTTP_MAX_HEADERS       64
#define HTTP_MAX_HEADER_NAME   128
#define HTTP_MAX_HEADER_VALUE  4096
#define HTTP_MAX_PATH          2048
#define HTTP_MAX_BODY          (16 * 1024 * 1024)

/* ── Header ────────────────────────────────────────────────────────────── */
typedef struct {
    char name[HTTP_MAX_HEADER_NAME];
    char value[HTTP_MAX_HEADER_VALUE];
} HttpHeader;

/* ── Request ───────────────────────────────────────────────────────────── */
typedef struct {
    HttpMethod method;
    char path[HTTP_MAX_PATH];
    char query_string[HTTP_MAX_PATH];
    HttpHeader headers[HTTP_MAX_HEADERS];
    int header_count;
    char *body;
    size_t body_len;
} HttpRequest;

/* ── Response ──────────────────────────────────────────────────────────── */
typedef struct {
    int status_code;
    HttpHeader headers[HTTP_MAX_HEADERS];
    int header_count;
    char *body;
    size_t body_len;
    bool headers_sent;
} HttpResponse;

/* ── Function Declarations ─────────────────────────────────────────────── */
const char *http_method_str(HttpMethod method);
const char *http_status_text(int status_code);

HttpMethod http_method_from_str(const char *str);

void http_request_init(HttpRequest *req);
void http_request_free(HttpRequest *req);
bool http_parse_request_line(const char *line, HttpRequest *req);
bool http_parse_header(const char *line, HttpRequest *req);
const char *http_request_get_header(const HttpRequest *req, const char *name);

void http_response_init(HttpResponse *res);
void http_response_free(HttpResponse *res);
void http_response_set_status(HttpResponse *res, int code);
void http_response_add_header(HttpResponse *res, const char *name, const char *value);
void http_response_set_body(HttpResponse *res, const char *body, size_t len);
void http_response_set_body_str(HttpResponse *res, const char *body);
int  http_serialize_response(const HttpResponse *res, char *buf, size_t buf_size);

const char *http_status_message(int code);
bool http_parse_query_string(const char *query, char *key_buf, size_t key_sz,
                              char *val_buf, size_t val_sz, const char *key);
bool http_url_decode(const char *src, char *dst, size_t dst_size);

#endif /* HTTP_CORE_H */
