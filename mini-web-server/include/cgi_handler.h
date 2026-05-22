#ifndef CGI_HANDLER_H
#define CGI_HANDLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "http_core.h"

/* ── Max Constants ──────────────────────────────────────────────────────── */
#define CGI_MAX_ENV_VARS       64
#define CGI_MAX_ENV_KEY       128
#define CGI_MAX_ENV_VALUE    4096
#define CGI_MAX_SCRIPT_PATH  1024
#define CGI_MAX_OUTPUT       (8 * 1024 * 1024)
#define CGI_TIMEOUT_SECONDS    30
#define CGI_MAX_RESPONSE_LINE 8192

/* ── CGI Environment Variable ───────────────────────────────────────────── */
typedef struct {
    char key[CGI_MAX_ENV_KEY];
    char value[CGI_MAX_ENV_VALUE];
} CgiEnvVar;

/* ── CGI Configuration ──────────────────────────────────────────────────── */
typedef struct {
    char script_path[CGI_MAX_SCRIPT_PATH];
    CgiEnvVar env_vars[CGI_MAX_ENV_VARS];
    int  env_count;
    bool pass_headers;
    bool pass_body;
    int  timeout_seconds;
} CgiConfig;

/* ── CGI Result ─────────────────────────────────────────────────────────── */
typedef struct {
    int  exit_code;
    char stdout_data[CGI_MAX_OUTPUT];
    size_t stdout_len;
    char stderr_data[CGI_MAX_OUTPUT];
    size_t stderr_len;
    int  signal_number;
    bool timed_out;
} CgiResult;

/* ── FastCGI Record Header (simulation) ─────────────────────────────────── */
typedef enum {
    FCGI_BEGIN_REQUEST    =  1,
    FCGI_ABORT_REQUEST    =  2,
    FCGI_END_REQUEST      =  3,
    FCGI_PARAMS           =  4,
    FCGI_STDIN            =  5,
    FCGI_STDOUT           =  6,
    FCGI_STDERR           =  7,
    FCGI_DATA             =  8,
    FCGI_GET_VALUES       =  9,
    FCGI_GET_VALUES_RESULT = 10,
    FCGI_UNKNOWN_TYPE     = 11
} FcgiRecordType;

typedef enum {
    FCGI_RESPONDER  = 1,
    FCGI_AUTHORIZER = 2,
    FCGI_FILTER     = 3
} FcgiRole;

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint16_t request_id;
    uint16_t content_length;
    uint8_t  padding_length;
    uint8_t  reserved;
} FcgiHeader;

typedef struct {
    uint16_t role;
    uint8_t  flags;
    uint8_t  reserved[5];
} FcgiBeginRequestBody;

typedef struct {
    uint32_t app_status;
    uint8_t  protocol_status;
    uint8_t  reserved[3];
} FcgiEndRequestBody;

/* ── Function Declarations ──────────────────────────────────────────────── */
void  cgi_config_init(CgiConfig *cfg, const char *script_path);
void  cgi_config_add_env(CgiConfig *cfg, const char *key, const char *value);
void  cgi_config_set_request_env(CgiConfig *cfg, const HttpRequest *req);
bool  cgi_execute(const CgiConfig *cfg, const HttpRequest *req,
                   CgiResult *result);
bool  cgi_parse_status_line(const char *data, size_t len, int *status_code,
                             char *status_text, size_t text_sz);
void  cgi_parse_headers(const CgiResult *result, HttpResponse *res);
bool  cgi_result_to_response(const CgiResult *result, HttpResponse *res);

/* ── FastCGI Helpers (simulation) ───────────────────────────────────────── */
void  fcgi_build_header(uint8_t type, uint16_t request_id,
                         uint16_t content_len, uint8_t padding_len,
                         FcgiHeader *header);
void  fcgi_build_begin_request(uint16_t role, uint8_t flags,
                                FcgiBeginRequestBody *body);
bool  fcgi_parse_header(const uint8_t *data, size_t len, FcgiHeader *header);
bool  fcgi_parse_end_request(const uint8_t *data, FcgiEndRequestBody *body);

#endif /* CGI_HANDLER_H */
