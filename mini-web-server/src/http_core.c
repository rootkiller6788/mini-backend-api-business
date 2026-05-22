#include "http_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *g_method_strings[] = {
    "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH", "UNKNOWN"
};

static const char *g_status_messages[] = {
    [200] = "OK",
    [201] = "Created",
    [204] = "No Content",
    [206] = "Partial Content",
    [301] = "Moved Permanently",
    [302] = "Found",
    [304] = "Not Modified",
    [400] = "Bad Request",
    [401] = "Unauthorized",
    [403] = "Forbidden",
    [404] = "Not Found",
    [405] = "Method Not Allowed",
    [408] = "Request Timeout",
    [409] = "Conflict",
    [410] = "Gone",
    [413] = "Payload Too Large",
    [414] = "URI Too Long",
    [415] = "Unsupported Media Type",
    [416] = "Range Not Satisfiable",
    [429] = "Too Many Requests",
    [500] = "Internal Server Error",
    [501] = "Not Implemented",
    [502] = "Bad Gateway",
    [503] = "Service Unavailable",
    [504] = "Gateway Timeout",
};

const char *http_method_str(HttpMethod method) {
    if (method < 0 || method > HTTP_UNKNOWN) return "UNKNOWN";
    return g_method_strings[method];
}

const char *http_status_text(int status_code) {
    if (status_code < 0 || status_code > 599) return "Unknown";
    if (status_code >= 0 && status_code < (int)(sizeof(g_status_messages) /
        sizeof(g_status_messages[0]))) {
        if (g_status_messages[status_code]) return g_status_messages[status_code];
    }
    return "Unknown";
}

HttpMethod http_method_from_str(const char *str) {
    for (int i = 0; i < HTTP_UNKNOWN; i++) {
        if (strcmp(str, g_method_strings[i]) == 0) return (HttpMethod)i;
    }
    return HTTP_UNKNOWN;
}

void http_request_init(HttpRequest *req) {
    memset(req, 0, sizeof(*req));
    req->method = HTTP_UNKNOWN;
}

void http_request_free(HttpRequest *req) {
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
}

bool http_parse_request_line(const char *line, HttpRequest *req) {
    char method_str[32] = {0};
    char path_raw[HTTP_MAX_PATH] = {0};
    char version[16] = {0};

    int n = sscanf(line, "%31s %2047s %15s", method_str, path_raw, version);
    if (n < 2) return false;

    req->method = http_method_from_str(method_str);

    char *qmark = strchr(path_raw, '?');
    if (qmark) {
        *qmark = '\0';
        strncpy(req->query_string, qmark + 1, HTTP_MAX_PATH - 1);
    }
    http_url_decode(path_raw, req->path, HTTP_MAX_PATH);
    return true;
}

bool http_parse_header(const char *line, HttpRequest *req) {
    if (req->header_count >= HTTP_MAX_HEADERS) return false;
    const char *colon = strchr(line, ':');
    if (!colon) return false;

    size_t name_len = (size_t)(colon - line);
    if (name_len >= HTTP_MAX_HEADER_NAME) name_len = HTTP_MAX_HEADER_NAME - 1;
    memcpy(req->headers[req->header_count].name, line, name_len);
    req->headers[req->header_count].name[name_len] = '\0';

    const char *val = colon + 1;
    while (*val == ' ' || *val == '\t') val++;
    size_t val_len = strlen(val);
    while (val_len > 0 && (val[val_len - 1] == '\r' || val[val_len - 1] == '\n'
           || val[val_len - 1] == ' ')) val_len--;
    if (val_len >= HTTP_MAX_HEADER_VALUE) val_len = HTTP_MAX_HEADER_VALUE - 1;
    memcpy(req->headers[req->header_count].value, val, val_len);
    req->headers[req->header_count].value[val_len] = '\0';

    req->header_count++;
    return true;
}

const char *http_request_get_header(const HttpRequest *req, const char *name) {
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, name) == 0)
            return req->headers[i].value;
    }
    return NULL;
}

void http_response_init(HttpResponse *res) {
    memset(res, 0, sizeof(*res));
    res->status_code = HTTP_STATUS_OK;
}

void http_response_free(HttpResponse *res) {
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
}

void http_response_set_status(HttpResponse *res, int code) {
    res->status_code = code;
}

void http_response_add_header(HttpResponse *res, const char *name,
                               const char *value) {
    if (res->header_count >= HTTP_MAX_HEADERS) return;
    strncpy(res->headers[res->header_count].name, name, HTTP_MAX_HEADER_NAME - 1);
    strncpy(res->headers[res->header_count].value, value, HTTP_MAX_HEADER_VALUE - 1);
    res->header_count++;
}

void http_response_set_body(HttpResponse *res, const char *body, size_t len) {
    free(res->body);
    res->body = malloc(len + 1);
    if (!res->body) return;
    memcpy(res->body, body, len);
    res->body[len] = '\0';
    res->body_len = len;
}

void http_response_set_body_str(HttpResponse *res, const char *body) {
    http_response_set_body(res, body, strlen(body));
}

int http_serialize_response(const HttpResponse *res, char *buf, size_t buf_size) {
    int off = snprintf(buf, buf_size, "HTTP/1.1 %d %s\r\n",
                       res->status_code, http_status_text(res->status_code));
    if (off < 0) return -1;

    for (int i = 0; i < res->header_count; i++) {
        int n = snprintf(buf + off, buf_size - off, "%s: %s\r\n",
                         res->headers[i].name, res->headers[i].value);
        if (n < 0) return -1;
        off += n;
    }

    if (res->body_len > 0) {
        int n = snprintf(buf + off, buf_size - off,
                         "Content-Length: %zu\r\n", res->body_len);
        if (n < 0) return -1;
        off += n;
    }

    off += snprintf(buf + off, buf_size - off, "\r\n");

    if (res->body && res->body_len > 0 && (size_t)off < buf_size) {
        size_t remain = buf_size - off;
        size_t copy = res->body_len < remain ? res->body_len : remain - 1;
        memcpy(buf + off, res->body, copy);
        off += (int)copy;
    }
    return off;
}

const char *http_status_message(int code) {
    return http_status_text(code);
}

bool http_parse_query_string(const char *query, char *key_buf, size_t key_sz,
                              char *val_buf, size_t val_sz, const char *key) {
    if (!query || !key) return false;
    const char *p = query;
    while (*p) {
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');
        if (!amp) amp = p + strlen(p);
        if (!eq || eq > amp) { p = (*amp) ? amp + 1 : amp; continue; }

        size_t klen = (size_t)(eq - p);
        if (klen == strlen(key) && strncmp(p, key, klen) == 0) {
            const char *vstart = eq + 1;
            size_t vlen = (size_t)(amp - vstart);
            if (vlen >= val_sz) vlen = val_sz - 1;
            memcpy(val_buf, vstart, vlen);
            val_buf[vlen] = '\0';
            http_url_decode(val_buf, key_buf, key_sz);
            return true;
        }
        p = (*amp) ? amp + 1 : amp;
    }
    return false;
}

bool http_url_decode(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return false;
    size_t i = 0;
    while (*src && i < dst_size - 1) {
        if (*src == '%' && isxdigit((unsigned char)src[1])
            && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
    return true;
}
