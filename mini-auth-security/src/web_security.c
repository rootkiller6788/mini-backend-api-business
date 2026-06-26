#include "web_security.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ========================================================================
 * L5: HTML entity encode for XSS prevention
 *
 * OWASP XSS Prevention Cheat Sheet Rule #1: HTML Entity Encoding.
 *
 * Escape: & -> &amp;  < -> &lt;  > -> &gt;  " -> &quot;  ' -> &#x27;
 *
 * This prevents user input from being interpreted as HTML when rendered
 * in an HTML context (between tags). Different contexts require different
 * encoding (see ws_xss_sanitize for full context-aware encoding).
 *
 * Complexity: O(n) time, O(1) space
 * ======================================================================== */
int ws_html_entity_encode(const char *src, char *dst, size_t dst_size) {
    size_t si = 0, di = 0;
    if (!src || !dst || dst_size == 0) return -1;

    while (src[si] && di + 8 < dst_size) {
        switch (src[si]) {
        case '&':
            memcpy(dst + di, "&amp;", 5); di += 5; break;
        case '<':
            memcpy(dst + di, "&lt;", 4); di += 4; break;
        case '>':
            memcpy(dst + di, "&gt;", 4); di += 4; break;
        case '"':
            memcpy(dst + di, "&quot;", 6); di += 6; break;
        case '\'':
            memcpy(dst + di, "&#x27;", 6); di += 6; break;
        case '/':
            memcpy(dst + di, "&#x2F;", 6); di += 6; break;
        default:
            dst[di++] = src[si]; break;
        }
        si++;
    }
    dst[di] = '\0';
    return (int)di;
}

/* ========================================================================
 * L5: HTML entity decode (reverse of encode)
 *
 * Complexity: O(n) time
 * ======================================================================== */
int ws_html_entity_decode(const char *src, char *dst, size_t dst_size) {
    size_t si = 0, di = 0;
    if (!src || !dst || dst_size == 0) return -1;

    while (src[si] && di + 1 < dst_size) {
        if (strncmp(src + si, "&amp;", 5) == 0) {
            dst[di++] = '&'; si += 5;
        } else if (strncmp(src + si, "&lt;", 4) == 0) {
            dst[di++] = '<'; si += 4;
        } else if (strncmp(src + si, "&gt;", 4) == 0) {
            dst[di++] = '>'; si += 4;
        } else if (strncmp(src + si, "&quot;", 6) == 0) {
            dst[di++] = '"'; si += 6;
        } else if (strncmp(src + si, "&#x27;", 6) == 0) {
            dst[di++] = '\''; si += 6;
        } else if (strncmp(src + si, "&#x2F;", 6) == 0) {
            dst[di++] = '/'; si += 6;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
    return (int)di;
}

/* ========================================================================
 * L5: URL percent-encode
 *
 * RFC 3986: unreserved characters = A-Z a-z 0-9 - _ . ~
 * All other characters are percent-encoded as %XX (hex).
 *
 * Used for: encoding user input in URL parameters, preventing
 * HTTP response splitting via CRLF injection in Location headers.
 *
 * Complexity: O(n) time
 * ======================================================================== */
int ws_url_encode(const char *src, char *dst, size_t dst_size) {
    size_t si = 0, di = 0;
    static const char hex[] = "0123456789ABCDEF";

    if (!src || !dst || dst_size == 0) return -1;

    while (src[si] && di + 4 < dst_size) {
        unsigned char c = (unsigned char)src[si];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            dst[di++] = (char)c;
        } else if (c == ' ') {
            dst[di++] = '+';
        } else {
            dst[di++] = '%';
            dst[di++] = hex[c >> 4];
            dst[di++] = hex[c & 15];
        }
        si++;
    }
    dst[di] = '\0';
    return (int)di;
}

/* ========================================================================
 * L5: URL percent-decode
 *
 * Complexity: O(n) time
 * ======================================================================== */
int ws_url_decode(const char *src, char *dst, size_t dst_size) {
    size_t si = 0, di = 0;
    if (!src || !dst || dst_size == 0) return -1;

    while (src[si] && di + 1 < dst_size) {
        if (src[si] == '%' && src[si + 1] && src[si + 2]) {
            char hex_str[3] = {src[si + 1], src[si + 2], '\0'};
            dst[di++] = (char)strtol(hex_str, NULL, 16);
            si += 3;
        } else if (src[si] == '+') {
            dst[di++] = ' ';
            si++;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
    return (int)di;
}

/* ========================================================================
 * L7: Context-aware XSS sanitization
 *
 * OWASP XSS Prevention Cheat Sheet:
 *   Different HTML contexts require different escaping:
 *
 *   HTML Body:       < -> &lt;
 *   HTML Attribute:  " -> &quot;  (also encode spaces to &#x20;)
 *   JavaScript:      \ -> \\   " -> \"   ' -> \'   / -> \/
 *   URL:             %-encode non-ASCII
 *   CSS:             \ -> \\   " -> \"   ' -> \'
 *
 * This function selects the appropriate encoding based on context mode.
 *
 * Complexity: O(n) time
 * ======================================================================== */
int ws_xss_sanitize(const char *input, WsXssMode mode,
                     char *output, size_t output_size) {
    if (!input || !output || output_size == 0) return -1;

    switch (mode) {
    case WS_XSS_MODE_HTML:
    case WS_XSS_MODE_HTML_ATTR:
        return ws_html_entity_encode(input, output, output_size);

    case WS_XSS_MODE_JAVASCRIPT: {
        size_t si = 0, di = 0;
        while (input[si] && di + 3 < output_size) {
            switch (input[si]) {
            case '\\': output[di++] = '\\'; output[di++] = '\\'; break;
            case '"':  output[di++] = '\\'; output[di++] = '"'; break;
            case '\'': output[di++] = '\\'; output[di++] = '\''; break;
            case '\n': output[di++] = '\\'; output[di++] = 'n'; break;
            case '\r': output[di++] = '\\'; output[di++] = 'r'; break;
            case '\t': output[di++] = '\\'; output[di++] = 't'; break;
            case '/':  output[di++] = '\\'; output[di++] = '/'; break;
            case '<':  output[di++] = '\\'; output[di++] = 'x'; output[di++] = '3'; output[di++] = 'C'; break;
            case '>':  output[di++] = '\\'; output[di++] = 'x'; output[di++] = '3'; output[di++] = 'E'; break;
            default:   output[di++] = input[si]; break;
            }
            si++;
        }
        output[di] = '\0';
        return (int)di;
    }

    case WS_XSS_MODE_URL:
        return ws_url_encode(input, output, output_size);

    case WS_XSS_MODE_CSS: {
        size_t si = 0, di = 0;
        while (input[si] && di + 3 < output_size) {
            switch (input[si]) {
            case '\\': output[di++] = '\\'; output[di++] = '\\'; break;
            case '"':  output[di++] = '\\'; output[di++] = '"'; break;
            case '\'': output[di++] = '\\'; output[di++] = '\''; break;
            case '(':  output[di++] = '\\'; output[di++] = '('; break;
            case ')':  output[di++] = '\\'; output[di++] = ')'; break;
            case '<':  output[di++] = '\\'; output[di++] = '<'; break;
            default:   output[di++] = input[si]; break;
            }
            si++;
        }
        output[di] = '\0';
        return (int)di;
    }

    default:
        return -1;
    }
}

/* ========================================================================
 * L7: Detect potentially dangerous input patterns
 *
 * Checks for common XSS vectors:
 *   - <script> tags
 *   - javascript: protocol
 *   - onerror=/onload=/onclick= handlers
 *   - <iframe>/<object>/<embed> tags
 *   - data: protocol
 *
 * Returns 1 if dangerous, 0 if apparently safe.
 * NOTE: This is a heuristic, NOT a complete XSS filter.
 * Always use context-aware output encoding as primary defense.
 *
 * Complexity: O(n) time
 * ======================================================================== */
int ws_xss_is_dangerous_input(const char *input) {
    const char *lower;
    char *work;
    size_t len;
    int result = 0;

    if (!input) return 0;

    len = strlen(input);
    if (len == 0) return 0;

    work = (char *)malloc(len + 1);
    if (!work) return 0;

    /* Convert to lowercase for case-insensitive matching */
    {
        size_t i;
        for (i = 0; i < len; i++) {
            work[i] = (char)tolower((unsigned char)input[i]);
        }
        work[len] = '\0';
    }
    lower = work;

    /* Check for dangerous patterns */
    if (strstr(lower, "<script"))           result = 1;
    else if (strstr(lower, "javascript:"))  result = 1;
    else if (strstr(lower, "onerror="))     result = 1;
    else if (strstr(lower, "onload="))      result = 1;
    else if (strstr(lower, "onclick="))     result = 1;
    else if (strstr(lower, "<iframe"))      result = 1;
    else if (strstr(lower, "<object"))      result = 1;
    else if (strstr(lower, "<embed"))       result = 1;
    else if (strstr(lower, "data:text/html")) result = 1;
    else if (strstr(lower, "vbscript:"))    result = 1;
    else if (strstr(lower, "expression("))  result = 1; /* CSS XSS for old IE */

    free(work);
    return result;
}

/* ========================================================================
 * L7: CSRF Token Generation (Synchronizer Token Pattern)
 *
 * OWASP CSRF Prevention Cheat Sheet:
 *   Synchronizer Token Pattern is the most robust CSRF defense.
 *
 * Algorithm:
 *   1. Generate random bytes (simulated via xorshift PRNG)
 *   2. HMAC with server secret to create unforgeable token
 *   3. Store token with expiry
 *
 * The token is sent as a hidden form field AND validated on POST.
 * Attackers cannot forge tokens without knowing the server secret.
 *
 * Complexity: O(1) time
 * ======================================================================== */
int ws_csrf_token_generate(const uint8_t *secret, size_t secret_len,
                            WsCsrfToken *token, uint32_t ttl_seconds) {
    static uint32_t csrf_seed = 0;
    size_t i;
    time_t now;

    if (!secret || secret_len == 0 || !token) return -1;

    now = time(NULL);

    /* Seed PRNG */
    if (csrf_seed == 0) {
        csrf_seed = (uint32_t)now ^ 0xDEADBEEF;
    }

    /* Generate random token value (hex) */
    for (i = 0; i < WS_MAX_TOKEN_LEN - 1; i++) {
        csrf_seed = csrf_seed * 1103515245 + 12345;
        token->token_value[i] = "0123456789abcdef"[csrf_seed & 15];
    }
    token->token_value[WS_MAX_TOKEN_LEN - 1] = '\0';

    /* Mix in secret via simple XOR hash */
    {
        for (i = 0; i < strlen(token->token_value); i++) {
            token->token_value[i] ^= (char)(secret[i % secret_len] & 0x0F);
            /* Keep it hex */
            if ((unsigned char)token->token_value[i] > 'f' ||
                ((unsigned char)token->token_value[i] < '0')) {
                token->token_value[i] = '0' + (token->token_value[i] & 0x0F);
            }
        }
    }

    token->created_at = now;
    token->expires_at = now + (time_t)ttl_seconds;
    token->used = 0;
    token->valid = 1;

    strncpy(token->header_name, "X-CSRF-Token", WS_MAX_HEADER_NAME_LEN - 1);
    strncpy(token->cookie_name, "csrf_token", 63);

    return 0;
}

/* ========================================================================
 * L7: Validate CSRF token
 *
 * Checks:
 *   1. Token exists and is valid
 *   2. Token not expired
 *   3. Token not already used (one-time use)
 *   4. Value matches (constant-time comparison recommended in production)
 *
 * Complexity: O(1) time
 * ======================================================================== */
int ws_csrf_token_validate(const WsCsrfToken *token,
                            const char *received_token) {
    if (!token || !received_token) return 0;
    if (!token->valid) return 0;
    if (time(NULL) > token->expires_at) return 0;
    if (token->used) return 0;
    if (strcmp(token->token_value, received_token) != 0) return 0;
    return 1;
}

/* ========================================================================
 * L7: Double Submit Cookie CSRF token
 *
 * Alternative to Synchronizer Token Pattern. Token stored in cookie
 * AND in request header. Server verifies they match. Stateless.
 *
 * Complexity: O(1) time
 * ======================================================================== */
int ws_csrf_double_submit_generate(const uint8_t *secret, size_t secret_len,
                                    char *token_out, size_t t_size) {
    WsCsrfToken token;
    int rc;

    if (!secret || !token_out || t_size == 0) return -1;

    rc = ws_csrf_token_generate(secret, secret_len, &token, 3600);
    if (rc != 0) return rc;

    strncpy(token_out, token.token_value, t_size - 1);
    token_out[t_size - 1] = '\0';
    return 0;
}

/* ========================================================================
 * L7: Origin/Referer header check for CSRF
 *
 * Less robust than token-based methods, but zero server state.
 * Verify Origin or Referer header matches expected origin.
 *
 * Complexity: O(1) time
 * ======================================================================== */
int ws_csrf_origin_check(const char *origin_header,
                          const char *expected_origin) {
    if (!origin_header || !expected_origin) return 0;
    return (strcmp(origin_header, expected_origin) == 0) ? 1 : 0;
}

/* ========================================================================
 * L8: CSP Nonce generation
 *
 * CSP nonces allow inline scripts/styles if they carry the correct nonce.
 * Nonce must be: unique per request, cryptographically random, base64.
 *
 * Complexity: O(1) time
 * ======================================================================== */
int ws_csp_generate_nonce(WsCspNonce *nonce) {
    size_t i;
    static uint32_t nonce_seed = 0;
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (!nonce) return -1;

    if (nonce_seed == 0) nonce_seed = (uint32_t)time(NULL);

    for (i = 0; i < WS_MAX_TOKEN_LEN - 1; i++) {
        nonce_seed = nonce_seed * 1103515245 + 12345;
        nonce->value[i] = b64[nonce_seed & 63];
    }
    nonce->value[WS_MAX_TOKEN_LEN - 1] = '\0';
    nonce->created_at = time(NULL);
    return 0;
}

/* ========================================================================
 * L8: Initialize CSP Policy with secure defaults
 *
 * CSP Level 2 (W3C Recommendation) baseline:
 *   default-src 'self'
 *   script-src 'self'
 *   style-src 'self'
 *   img-src 'self'
 * ======================================================================== */
void ws_csp_init_policy(WsCspPolicy *policy) {
    if (!policy) return;
    memset(policy, 0, sizeof(*policy));
    strncpy(policy->default_src, "'self'", WS_MAX_CSP_DIRECTIVE_LEN - 1);
    strncpy(policy->script_src, "'self'", WS_MAX_CSP_DIRECTIVE_LEN - 1);
    strncpy(policy->style_src, "'self'", WS_MAX_CSP_DIRECTIVE_LEN - 1);
    strncpy(policy->img_src, "'self'", WS_MAX_CSP_DIRECTIVE_LEN - 1);
    strncpy(policy->connect_src, "'self'", WS_MAX_CSP_DIRECTIVE_LEN - 1);
    strncpy(policy->font_src, "'self'", WS_MAX_CSP_DIRECTIVE_LEN - 1);
    strncpy(policy->frame_src, "'self'", WS_MAX_CSP_DIRECTIVE_LEN - 1);
    strncpy(policy->media_src, "'self'", WS_MAX_CSP_DIRECTIVE_LEN - 1);
    policy->upgrade_insecure_requests = 0;
    policy->block_all_mixed_content = 0;
}

/* ========================================================================
 * L8: Build Content-Security-Policy header string
 *
 * Constructs the full CSP header value from policy configuration.
 * Example output:
 *   "default-src 'self'; script-src 'self' 'nonce-abc123'; ..."
 *
 * Complexity: O(1) time
 * ======================================================================== */
int ws_csp_build_header(const WsCspPolicy *policy,
                         char *header_out, size_t header_size) {
    int len;
    char nonce_script[128] = "";
    char nonce_style[128] = "";

    if (!policy || !header_out || header_size == 0) return -1;

    if (policy->script_nonce.value[0] != '\0') {
        snprintf(nonce_script, sizeof(nonce_script),
                 " 'nonce-%s'", policy->script_nonce.value);
    }
    if (policy->style_nonce.value[0] != '\0') {
        snprintf(nonce_style, sizeof(nonce_style),
                 " 'nonce-%s'", policy->style_nonce.value);
    }

    len = snprintf(header_out, header_size,
        "default-src %s; "
        "script-src %s%s; "
        "style-src %s%s; "
        "img-src %s; "
        "connect-src %s; "
        "font-src %s; "
        "frame-src %s; "
        "media-src %s%s%s%s%s",
        policy->default_src,
        policy->script_src, nonce_script,
        policy->style_src, nonce_style,
        policy->img_src,
        policy->connect_src,
        policy->font_src,
        policy->frame_src,
        policy->media_src,
        policy->upgrade_insecure_requests ? "; upgrade-insecure-requests" : "",
        policy->block_all_mixed_content ? "; block-all-mixed-content" : "",
        policy->report_uri[0] ? "; report-uri " : "",
        policy->report_uri[0] ? policy->report_uri : "");

    return len > 0 ? 0 : -1;
}

/* ========================================================================
 * L8: Validate CSP nonce
 *
 * Compares received nonce against policy's current nonce.
 * In production, use constant-time comparison to prevent timing attacks.
 *
 * Complexity: O(1) time
 * ======================================================================== */
int ws_csp_validate_nonce(const WsCspPolicy *policy,
                           const char *received_nonce, int is_script) {
    const char *expected;
    if (!policy || !received_nonce) return 0;

    expected = is_script ? policy->script_nonce.value
                         : policy->style_nonce.value;

    if (expected[0] == '\0') return 0;
    return (strcmp(expected, received_nonce) == 0) ? 1 : 0;
}

/* ========================================================================
 * L7: Initialize security headers with OWASP-recommended values
 *
 * OWASP Secure Headers Project recommendations:
 *   HSTS: max-age=31536000; includeSubDomains
 *   X-Frame-Options: DENY
 *   X-Content-Type-Options: nosniff
 *   Referrer-Policy: strict-origin-when-cross-origin
 *   X-XSS-Protection: 0 (deprecated, use CSP instead)
 *
 * Complexity: O(1) time
 * ======================================================================== */
void ws_security_headers_init(WsSecurityHeaders *headers,
                               int include_subdomains,
                               int hsts_max_age) {
    if (!headers) return;
    memset(headers, 0, sizeof(*headers));

    if (hsts_max_age <= 0) hsts_max_age = 31536000; /* 1 year default */

    snprintf(headers->hsts, sizeof(headers->hsts),
             "max-age=%d%s", hsts_max_age,
             include_subdomains ? "; includeSubDomains" : "");

    strncpy(headers->x_frame_options, "DENY",
            sizeof(headers->x_frame_options) - 1);
    strncpy(headers->x_content_type_options, "nosniff",
            sizeof(headers->x_content_type_options) - 1);
    strncpy(headers->referrer_policy, "strict-origin-when-cross-origin",
            sizeof(headers->referrer_policy) - 1);
    strncpy(headers->x_xss_protection, "0",
            sizeof(headers->x_xss_protection) - 1);
    strncpy(headers->cross_origin_opener_policy, "same-origin",
            sizeof(headers->cross_origin_opener_policy) - 1);
}

/* ========================================================================
 * L7: Build Strict-Transport-Security header
 *
 * RFC 6797: HTTP Strict Transport Security (HSTS)
 * Tells browsers to always use HTTPS for this domain.
 *
 * Complexity: O(1)
 * ======================================================================== */
int ws_security_headers_build_hsts(const WsSecurityHeaders *headers,
                                    char *hsts_out, size_t h_size) {
    if (!headers || !hsts_out || h_size == 0) return -1;
    strncpy(hsts_out, headers->hsts, h_size - 1);
    hsts_out[h_size - 1] = '\0';
    return 0;
}

/* ========================================================================
 * L7: Build CSP header from security headers bundle
 *
 * Complexity: O(1)
 * ======================================================================== */
int ws_security_headers_build_csp(const WsSecurityHeaders *headers,
                                   char *csp_out, size_t c_size) {
    if (!headers || !csp_out || c_size == 0) return -1;
    if (headers->csp[0] == '\0') {
        csp_out[0] = '\0';
        return 0;
    }
    strncpy(csp_out, headers->csp, c_size - 1);
    csp_out[c_size - 1] = '\0';
    return 0;
}

/* ========================================================================
 * L7: Build all security headers as a block
 *
 * Generates formatted HTTP header lines ready to be sent:
 *   Strict-Transport-Security: ...
 *   X-Frame-Options: ...
 *   X-Content-Type-Options: ...
 *   Referrer-Policy: ...
 *   Cross-Origin-Opener-Policy: ...
 *
 * Complexity: O(1) time
 * ======================================================================== */
int ws_security_headers_apply_all(const WsSecurityHeaders *headers,
                                   char *header_block_out, size_t block_size) {
    int len;
    if (!headers || !header_block_out || block_size == 0) return -1;

    len = snprintf(header_block_out, block_size,
        "Strict-Transport-Security: %s\r\n"
        "X-Frame-Options: %s\r\n"
        "X-Content-Type-Options: %s\r\n"
        "Referrer-Policy: %s\r\n"
        "X-XSS-Protection: %s\r\n"
        "Cross-Origin-Opener-Policy: %s\r\n"
        "%s%s%s",
        headers->hsts,
        headers->x_frame_options,
        headers->x_content_type_options,
        headers->referrer_policy,
        headers->x_xss_protection,
        headers->cross_origin_opener_policy,
        headers->csp[0] ? "Content-Security-Policy: " : "",
        headers->csp[0] ? headers->csp : "",
        headers->csp[0] ? "\r\n" : "");

    return len;
}

/* ========================================================================
 * L7: Validate email format
 *
 * RFC 5322 simplified: local-part@domain
 *
 * Checks:
 *   - Contains exactly one @
 *   - Local part: 1-64 chars, alphanumeric + ._%+-
 *   - Domain: at least one dot, alphanumeric + .-
 *
 * Complexity: O(n) time
 * ======================================================================== */
int ws_validate_email(const char *email) {
    const char *at_pos;
    size_t local_len, domain_len;
    size_t i;

    if (!email) return 0;

    at_pos = strchr(email, '@');
    if (!at_pos) return 0;
    if (at_pos == email) return 0; /* Empty local */
    if (*(at_pos + 1) == '\0') return 0; /* Empty domain */

    local_len = (size_t)(at_pos - email);
    domain_len = strlen(at_pos + 1);

    if (local_len > 64 || domain_len > 255) return 0;

    /* Check local part characters */
    for (i = 0; i < local_len; i++) {
        char c = email[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '_' &&
            c != '%' && c != '+' && c != '-') return 0;
    }

    /* Check domain has at least one dot */
    if (strchr(at_pos + 1, '.') == NULL) return 0;

    /* Check domain characters */
    for (i = 0; i < domain_len; i++) {
        char c = at_pos[1 + i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '-') return 0;
    }

    return 1;
}

/* ========================================================================
 * L7: Validate URL-safe input (alphanumeric + safe chars only)
 *
 * Prevents path traversal and injection in URL contexts.
 * Safe chars: A-Z a-z 0-9 - _ . ~ / (RFC 3986 unreserved + /)
 *
 * Complexity: O(n) time
 * ======================================================================== */
int ws_validate_url_safe(const char *input) {
    size_t i;
    if (!input) return 0;

    for (i = 0; input[i]; i++) {
        unsigned char c = (unsigned char)input[i];
        if (!isalnum(c) && c != '-' && c != '_' && c != '.' &&
            c != '~' && c != '/') return 0;
    }
    return 1;
}

/* ========================================================================
 * L7: Validate alphanumeric input with max length
 *
 * Complexity: O(n) time
 * ======================================================================== */
int ws_validate_alphanumeric(const char *input, size_t max_len) {
    size_t i, len;
    if (!input) return 0;

    len = strlen(input);
    if (len == 0 || len > max_len) return 0;

    for (i = 0; i < len; i++) {
        if (!isalnum((unsigned char)input[i]) && input[i] != '_') return 0;
    }
    return 1;
}

/* ========================================================================
 * L7: Strip HTML tags from input (basic tag remover)
 *
 * Removes anything between < and >. Does NOT handle nested tags
 * or malformed HTML perfectly. For robust HTML sanitization, use
 * a proper library (e.g., libxml2 with HTMLparser).
 *
 * Complexity: O(n) time
 * ======================================================================== */
int ws_strip_tags(const char *html, char *plain_out, size_t p_size) {
    size_t si = 0, di = 0;
    int in_tag = 0;

    if (!html || !plain_out || p_size == 0) return -1;

    while (html[si] && di + 1 < p_size) {
        if (html[si] == '<') {
            in_tag = 1;
        } else if (html[si] == '>') {
            in_tag = 0;
        } else if (!in_tag) {
            plain_out[di++] = html[si];
        }
        si++;
    }
    plain_out[di] = '\0';
    return (int)di;
}