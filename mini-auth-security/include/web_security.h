#ifndef WEB_SECURITY_H
#define WEB_SECURITY_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * web_security.h -- Web Application Security Protections
 *
 * Knowledge Layers:
 *   L1: Definitions -- CSRF token, XSS filter modes, security headers
 *   L2: Core Concepts -- SOP, CORS, CSP, CSRF, XSS, clickjacking
 *   L3: Engineering -- synchronizer token pattern, HTML context encoding
 *   L4: Standards -- OWASP Top 10, CSP Level 2 (W3C), RFC 6749 (OAuth2)
 *   L5: Algorithms -- HTML entity encoding, CSRF token HMAC
 *   L7: Applications -- web API security middleware
 *   L8: Advanced -- Content Security Policy generation, nonce-based CSP
 */

#define WS_MAX_TOKEN_LEN         128
#define WS_MAX_HEADER_NAME_LEN   64
#define WS_MAX_HEADER_VALUE_LEN  512
#define WS_MAX_CSP_DIRECTIVE_LEN 256
#define WS_MAX_URL_LEN           512
#define WS_MAX_TAG_LEN           64
#define WS_MAX_ATTR_LEN          64
#define WS_MAX_SANITIZED_LEN     4096

typedef enum {
    WS_XSS_MODE_HTML = 0,
    WS_XSS_MODE_HTML_ATTR,
    WS_XSS_MODE_JAVASCRIPT,
    WS_XSS_MODE_URL,
    WS_XSS_MODE_CSS
} WsXssMode;

typedef enum {
    WS_CSRF_MODE_SYNCHRONIZER = 0,
    WS_CSRF_MODE_DOUBLE_SUBMIT,
    WS_CSRF_MODE_ORIGIN_CHECK
} WsCsrfMode;

typedef enum {
    WS_REFERRER_NO_REFERRER = 0,
    WS_REFERRER_SAME_ORIGIN,
    WS_REFERRER_STRICT_ORIGIN,
    WS_REFERRER_ORIGIN_WHEN_CROSS_ORIGIN
} WsReferrerPolicy;

typedef struct {
    char token_value[WS_MAX_TOKEN_LEN];
    char header_name[WS_MAX_HEADER_NAME_LEN];
    char cookie_name[64];
    time_t created_at;
    time_t expires_at;
    int used;
    int valid;
} WsCsrfToken;

typedef struct {
    char value[WS_MAX_TOKEN_LEN];
    time_t created_at;
} WsCspNonce;

typedef struct {
    char default_src[WS_MAX_CSP_DIRECTIVE_LEN];
    char script_src[WS_MAX_CSP_DIRECTIVE_LEN];
    char style_src[WS_MAX_CSP_DIRECTIVE_LEN];
    char img_src[WS_MAX_CSP_DIRECTIVE_LEN];
    char connect_src[WS_MAX_CSP_DIRECTIVE_LEN];
    char font_src[WS_MAX_CSP_DIRECTIVE_LEN];
    char frame_src[WS_MAX_CSP_DIRECTIVE_LEN];
    char media_src[WS_MAX_CSP_DIRECTIVE_LEN];
    char report_uri[WS_MAX_URL_LEN];
    int  upgrade_insecure_requests;
    int  block_all_mixed_content;
    WsCspNonce script_nonce;
    WsCspNonce style_nonce;
} WsCspPolicy;

/*
 * Security header bundle conforming to OWASP recommendations.
 * See: https://owasp.org/www-project-secure-headers/
 */
typedef struct {
    char hsts[WS_MAX_HEADER_VALUE_LEN];
    char x_frame_options[WS_MAX_HEADER_VALUE_LEN];
    char x_content_type_options[WS_MAX_HEADER_VALUE_LEN];
    char referrer_policy[WS_MAX_HEADER_VALUE_LEN];
    char csp[WS_MAX_HEADER_VALUE_LEN * 4];
    char permissions_policy[WS_MAX_HEADER_VALUE_LEN];
    char x_xss_protection[WS_MAX_HEADER_VALUE_LEN];
    char cross_origin_opener_policy[WS_MAX_HEADER_VALUE_LEN];
} WsSecurityHeaders;

/* --- CSRF Protection --- */

int ws_csrf_token_generate(const uint8_t *secret, size_t secret_len,
                            WsCsrfToken *token, uint32_t ttl_seconds);

int ws_csrf_token_validate(const WsCsrfToken *token,
                            const char *received_token);

int ws_csrf_double_submit_generate(const uint8_t *secret, size_t secret_len,
                                    char *token_out, size_t t_size);

int ws_csrf_origin_check(const char *origin_header,
                          const char *expected_origin);

/* --- XSS Prevention --- */

int ws_xss_sanitize(const char *input, WsXssMode mode,
                     char *output, size_t output_size);

int ws_xss_is_dangerous_input(const char *input);

int ws_html_entity_encode(const char *src,
                           char *dst, size_t dst_size);

int ws_html_entity_decode(const char *src,
                           char *dst, size_t dst_size);

int ws_url_encode(const char *src, char *dst, size_t dst_size);
int ws_url_decode(const char *src, char *dst, size_t dst_size);

/* --- Content Security Policy --- */

void ws_csp_init_policy(WsCspPolicy *policy);

int ws_csp_generate_nonce(WsCspNonce *nonce);

int ws_csp_build_header(const WsCspPolicy *policy,
                         char *header_out, size_t header_size);

int ws_csp_validate_nonce(const WsCspPolicy *policy,
                           const char *received_nonce,
                           int is_script);

/* --- Security Headers --- */

void ws_security_headers_init(WsSecurityHeaders *headers,
                               int include_subdomains,
                               int hsts_max_age);

int ws_security_headers_build_hsts(const WsSecurityHeaders *headers,
                                    char *hsts_out, size_t h_size);
int ws_security_headers_build_csp(const WsSecurityHeaders *headers,
                                   char *csp_out, size_t c_size);

int ws_security_headers_apply_all(const WsSecurityHeaders *headers,
                                   char *header_block_out, size_t block_size);

/* --- Input Validation --- */

int ws_validate_email(const char *email);

int ws_validate_url_safe(const char *input);

int ws_validate_alphanumeric(const char *input, size_t max_len);

int ws_strip_tags(const char *html, char *plain_out, size_t p_size);

#endif /* WEB_SECURITY_H */
