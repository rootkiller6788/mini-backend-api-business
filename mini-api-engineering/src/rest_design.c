#include "rest_design.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

const char* rest_method_string(rest_method_t method) {
    static const char* names[] = {
        "GET", "POST", "PUT", "PATCH",
        "DELETE", "HEAD", "OPTIONS"
    };
    if (method >= REST_METHOD_COUNT) return "UNKNOWN";
    return names[method];
}

const char* rest_status_string(rest_status_t status) {
    switch (status) {
        case REST_200_OK:                   return "200 OK";
        case REST_201_CREATED:              return "201 Created";
        case REST_202_ACCEPTED:             return "202 Accepted";
        case REST_204_NO_CONTENT:           return "204 No Content";
        case REST_301_MOVED_PERMANENTLY:    return "301 Moved Permanently";
        case REST_302_FOUND:                return "302 Found";
        case REST_304_NOT_MODIFIED:         return "304 Not Modified";
        case REST_400_BAD_REQUEST:          return "400 Bad Request";
        case REST_401_UNAUTHORIZED:         return "401 Unauthorized";
        case REST_403_FORBIDDEN:            return "403 Forbidden";
        case REST_404_NOT_FOUND:            return "404 Not Found";
        case REST_405_METHOD_NOT_ALLOWED:   return "405 Method Not Allowed";
        case REST_409_CONFLICT:             return "409 Conflict";
        case REST_410_GONE:                 return "410 Gone";
        case REST_415_UNSUPPORTED_MEDIA:    return "415 Unsupported Media Type";
        case REST_422_UNPROCESSABLE_ENTITY: return "422 Unprocessable Entity";
        case REST_429_TOO_MANY_REQUESTS:    return "429 Too Many Requests";
        case REST_500_INTERNAL_SERVER_ERROR:return "500 Internal Server Error";
        case REST_502_BAD_GATEWAY:          return "502 Bad Gateway";
        case REST_503_SERVICE_UNAVAILABLE:  return "503 Service Unavailable";
        case REST_504_GATEWAY_TIMEOUT:      return "504 Gateway Timeout";
        default:                            return "Unknown Status";
    }
}

const char* rest_status_reason(rest_status_t status) {
    switch (status) {
        case REST_200_OK:                   return "OK";
        case REST_201_CREATED:              return "Created";
        case REST_202_ACCEPTED:             return "Accepted";
        case REST_204_NO_CONTENT:           return "No Content";
        case REST_301_MOVED_PERMANENTLY:    return "Moved Permanently";
        case REST_302_FOUND:                return "Found";
        case REST_304_NOT_MODIFIED:         return "Not Modified";
        case REST_400_BAD_REQUEST:          return "Bad Request";
        case REST_401_UNAUTHORIZED:         return "Unauthorized";
        case REST_403_FORBIDDEN:            return "Forbidden";
        case REST_404_NOT_FOUND:            return "Not Found";
        case REST_405_METHOD_NOT_ALLOWED:   return "Method Not Allowed";
        case REST_409_CONFLICT:             return "Conflict";
        case REST_410_GONE:                 return "Gone";
        case REST_415_UNSUPPORTED_MEDIA:    return "Unsupported Media Type";
        case REST_422_UNPROCESSABLE_ENTITY: return "Unprocessable Entity";
        case REST_429_TOO_MANY_REQUESTS:    return "Too Many Requests";
        case REST_500_INTERNAL_SERVER_ERROR:return "Internal Server Error";
        case REST_502_BAD_GATEWAY:          return "Bad Gateway";
        case REST_503_SERVICE_UNAVAILABLE:  return "Service Unavailable";
        case REST_504_GATEWAY_TIMEOUT:      return "Gateway Timeout";
        default:                            return "Unknown";
    }
}

void rest_resource_init(rest_resource_t* r, const char* name, const char* path, rest_method_t method) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    strncpy(r->name, name, sizeof(r->name) - 1);
    strncpy(r->path, path, sizeof(r->path) - 1);
    r->method = method;
    r->status = REST_200_OK;
    strcpy(r->content_type, "application/json");
    r->link_count = 0;
    r->header_count = 0;
    r->param_count = 0;
    rest_pagination_init(&r->pagination, REST_PAGE_DEFAULT, REST_PAGE_SIZE_MAX, 0);
}

void rest_resource_set_status(rest_resource_t* r, rest_status_t status) {
    if (!r) return;
    r->status = status;
}

void rest_resource_set_body(rest_resource_t* r, const char* body) {
    if (!r || !body) return;
    strncpy(r->response_body, body, sizeof(r->response_body) - 1);
    r->response_len = (int32_t)strlen(r->response_body);
}

void rest_resource_set_content_type(rest_resource_t* r, const char* ct) {
    if (!r || !ct) return;
    strncpy(r->content_type, ct, sizeof(r->content_type) - 1);
}

void rest_resource_add_link(rest_resource_t* r, const char* rel, const char* href, const char* method) {
    if (!r || r->link_count >= REST_MAX_LINKS) return;
    rest_link_t* link = &r->links[r->link_count++];
    strncpy(link->rel, rel, sizeof(link->rel) - 1);
    strncpy(link->href, href, sizeof(link->href) - 1);
    strncpy(link->method, method ? method : "GET", sizeof(link->method) - 1);
}

void rest_resource_add_header(rest_resource_t* r, const char* key, const char* value) {
    if (!r || r->header_count >= REST_MAX_HEADERS) return;
    strncpy(r->headers[r->header_count].key, key, sizeof(r->headers[0].key) - 1);
    strncpy(r->headers[r->header_count].value, value, sizeof(r->headers[0].value) - 1);
    r->header_count++;
}

void rest_resource_add_path_param(rest_resource_t* r, const char* name, const char* value) {
    if (!r || r->param_count >= REST_MAX_PATH_PARAMS) return;
    strncpy(r->params[r->param_count].name, name, sizeof(r->params[0].name) - 1);
    strncpy(r->params[r->param_count].value, value, sizeof(r->params[0].value) - 1);
    r->param_count++;
}

void rest_pagination_init(rest_pagination_t* p, int32_t page, int32_t page_size, int32_t total) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->page = page;
    p->page_size = page_size;
    p->total_items = total;
    p->total_pages = (page_size > 0) ? (total + page_size - 1) / page_size : 0;
    p->has_next = page < p->total_pages;
    p->has_prev = page > 1;
    if (p->has_prev) {
        snprintf(p->prev_link, sizeof(p->prev_link), "?page=%d&page_size=%d", page - 1, page_size);
    }
    if (p->has_next) {
        snprintf(p->next_link, sizeof(p->next_link), "?page=%d&page_size=%d", page + 1, page_size);
    }
    snprintf(p->first_link, sizeof(p->first_link), "?page=1&page_size=%d", page_size);
    snprintf(p->last_link, sizeof(p->last_link), "?page=%d&page_size=%d", p->total_pages, page_size);
}

void rest_router_init(rest_router_t* r, const char* base_path, int32_t version) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    strncpy(r->base_path, base_path, sizeof(r->base_path) - 1);
    r->base_path[sizeof(r->base_path) - 1] = '\0';
    r->version = version;
    snprintf(r->uri, sizeof(r->uri), "%s/v%d", base_path, version);
    r->rate_limit_rate = 100.0;
    r->rate_limit_capacity = 200.0;
    r->rate_limit_tokens = r->rate_limit_capacity;
    r->rate_limit_last_refill = (double)time(NULL);
}

bool rest_router_register(rest_router_t* r, rest_resource_t* resource) {
    if (!r || !resource) return false;
    if (r->route_count >= REST_MAX_ROUTES) return false;
    r->routes[r->route_count++] = resource;
    return true;
}

rest_resource_t* rest_router_resolve(rest_router_t* r, const char* uri, rest_method_t method) {
    if (!r || !uri) return NULL;
    rest_resource_t* best = NULL;
    int32_t best_score = -1;

    for (int32_t i = 0; i < r->route_count; i++) {
        rest_resource_t* res = r->routes[i];
        if (res->method != method) continue;

        const char* rpath = res->path;
        const char* upath = uri;
        int32_t score = 0;
        bool match = true;

        while (*rpath && *upath && match) {
            if (*rpath == '{') {
                while (*rpath && *rpath != '}') rpath++;
                if (*rpath == '}') rpath++;
                while (*upath && *upath != '/') upath++;
                score += 2;
            } else if (*rpath == *upath) {
                rpath++;
                upath++;
                score += 1;
            } else {
                match = false;
            }
        }
        if (*rpath == '\0' && *upath == '\0') match = true;
        else if (*rpath == '/' && *rpath && (*upath == '/' || *upath == '\0')) match = true;
        else if (*rpath == '\0' && *upath == '/') match = true;
        else if (*rpath == '/' && *(rpath+1) == '\0' && *upath == '\0') match = true;
        else match = false;

        if (match && score > best_score) {
            best_score = score;
            best = res;
        }
    }
    return best;
}

void rest_router_set_cors(rest_router_t* r, const char* origin, bool credentials,
                           const char* methods, const char* headers, int32_t max_age) {
    if (!r) return;
    strncpy(r->cors.origin, origin ? origin : "*", sizeof(r->cors.origin) - 1);
    r->cors.allow_credentials = credentials;
    if (methods) strncpy(r->cors.allowed_methods, methods, sizeof(r->cors.allowed_methods) - 1);
    if (headers) strncpy(r->cors.allowed_headers, headers, sizeof(r->cors.allowed_headers) - 1);
    r->cors.max_age = max_age;
}

void rest_router_set_rate_limit(rest_router_t* r, double rate, double capacity) {
    if (!r) return;
    r->rate_limit_rate = rate;
    r->rate_limit_capacity = capacity;
    r->rate_limit_tokens = capacity;
    r->rate_limit_last_refill = (double)time(NULL);
}

bool rest_router_check_rate_limit(rest_router_t* r) {
    if (!r) return false;
    if (r->rate_limit_rate <= 0.0) return true;

    double now = (double)time(NULL);
    double elapsed = now - r->rate_limit_last_refill;
    r->rate_limit_tokens += elapsed * r->rate_limit_rate;
    if (r->rate_limit_tokens > r->rate_limit_capacity)
        r->rate_limit_tokens = r->rate_limit_capacity;
    r->rate_limit_last_refill = now;

    if (r->rate_limit_tokens >= 1.0) {
        r->rate_limit_tokens -= 1.0;
        return true;
    }
    return false;
}

static uint32_t rest_djb2_hash(const char* str, int32_t len) {
    uint32_t hash = 5381;
    for (int32_t i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + (unsigned char)str[i];
    return hash;
}

rest_etag_t rest_etag_strong(const char* body, int32_t len) {
    rest_etag_t etag = { .value = "", .is_weak = false };
    if (!body || len <= 0) return etag;
    uint32_t h = rest_djb2_hash(body, len);
    snprintf(etag.value, sizeof(etag.value), "\"%08x-%d\"", h, len);
    return etag;
}

rest_etag_t rest_etag_weak(const char* body, int32_t len) {
    rest_etag_t etag = rest_etag_strong(body, len);
    if (etag.value[0] == '"') {
        char tmp[REST_ETAG_LEN];
        snprintf(tmp, sizeof(tmp), "W/%s", etag.value);
        strncpy(etag.value, tmp, sizeof(etag.value) - 1);
    }
    etag.is_weak = true;
    return etag;
}

bool rest_etag_match(rest_etag_t server_etag, const char* if_none_match) {
    if (!if_none_match || server_etag.value[0] == '\0') return false;
    if (strcmp(if_none_match, "*") == 0) return true;

    const char* p = if_none_match;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        bool weak = false;
        if (strncmp(p, "W/", 2) == 0) { weak = true; p += 2; }

        char tag[REST_ETAG_LEN] = {0};
        int32_t k = 0;
        bool quoted = false;
        if (*p == '"') { quoted = true; p++; }
        while (*p && *p != ',' && k < REST_ETAG_LEN - 1) {
            if (quoted && *p == '"') { p++; break; }
            tag[k++] = *p++;
        }
        tag[k] = '\0';

        if (weak || server_etag.is_weak) {
            if (strcmp(server_etag.value + (server_etag.is_weak ? 2 : 0),
                       tag) == 0) return true;
        } else {
            char tmp[REST_ETAG_LEN];
            snprintf(tmp, sizeof(tmp), "\"%s\"", tag);
            if (strcmp(server_etag.value, tmp) == 0) return true;
        }
    }
    return false;
}

int rest_etag_compare(rest_etag_t a, rest_etag_t b) {
    return strcmp(a.value, b.value);
}

const char* rest_media_type_to_mime(rest_media_type_t t) {
    static const char* mimes[] = {
        "application/json",
        "application/xml",
        "application/x-www-form-urlencoded",
        "multipart/form-data",
        "text/plain",
        "text/html",
        "application/octet-stream",
        "application/protobuf",
        "*/*"
    };
    if (t > REST_MEDIA_ANY) return "unknown";
    return mimes[t];
}

rest_media_range_t rest_parse_media_range(const char* accept_entry) {
    rest_media_range_t range = { .type = "", .subtype = "", .quality = 1.0, .level = 0 };
    if (!accept_entry) return range;

    const char* p = accept_entry;
    while (*p == ' ') p++;

    const char* ts = p;
    while (*p && *p != '/') p++;
    size_t tn = (size_t)(p - ts);
    if (tn >= sizeof(range.type)) tn = sizeof(range.type) - 1;
    memcpy(range.type, ts, tn);
    range.type[tn] = '\0';
    if (*p == '/') p++;

    const char* ss = p;
    while (*p && *p != ';' && *p != ',' && *p != ' ') p++;
    size_t sn = (size_t)(p - ss);
    if (sn >= sizeof(range.subtype)) sn = sizeof(range.subtype) - 1;
    memcpy(range.subtype, ss, sn);
    range.subtype[sn] = '\0';

    while (*p) {
        while (*p == ' ' || *p == ';') p++;
        if (strncmp(p, "q=", 2) == 0) {
            p += 2;
            range.quality = atof(p);
            while (*p && *p != ',' && *p != ';') p++;
        } else if (strncmp(p, "level=", 6) == 0) {
            p += 6;
            range.level = atoi(p);
            while (*p && *p != ',' && *p != ';') p++;
        } else {
            while (*p && *p != ',' && *p != ';') p++;
        }
    }
    return range;
}

static int rest_media_type_specificity(rest_media_type_t t) {
    switch (t) {
        case REST_MEDIA_JSON:         return 8;
        case REST_MEDIA_XML:          return 7;
        case REST_MEDIA_PROTOBUF:     return 6;
        case REST_MEDIA_MULTIPART:    return 5;
        case REST_MEDIA_FORM:         return 4;
        case REST_MEDIA_TEXT_HTML:    return 3;
        case REST_MEDIA_TEXT_PLAIN:   return 2;
        case REST_MEDIA_OCTET_STREAM: return 1;
        case REST_MEDIA_ANY:          return 0;
        default:                      return 0;
    }
}

rest_media_type_t rest_negotiate_content_type(const char* accept_header,
                                               const rest_media_type_t* supported, int32_t count) {
    if (!accept_header || !supported || count <= 0) {
        return count > 0 ? supported[0] : REST_MEDIA_JSON;
    }
    if (strcmp(accept_header, "*/*") == 0) return supported[0];

    rest_media_type_t best = supported[0];
    double best_q = -1.0;
    int best_spec = -1;

    char header_copy[1024];
    strncpy(header_copy, accept_header, sizeof(header_copy) - 1);
    header_copy[sizeof(header_copy) - 1] = '\0';

    char* token = strtok(header_copy, ",");
    while (token) {
        rest_media_range_t range = rest_parse_media_range(token);

        for (int32_t i = 0; i < count; i++) {
            const char* mime = rest_media_type_to_mime(supported[i]);
            char mime_type[64], mime_sub[64];
            const char* mp = mime;
            const char* ms = mp;
            while (*mp && *mp != '/') mp++;
            size_t mtn = (size_t)(mp - ms);
            if (mtn >= sizeof(mime_type)) mtn = sizeof(mime_type) - 1;
            memcpy(mime_type, ms, mtn);
            mime_type[mtn] = '\0';
            if (*mp == '/') mp++;
            strncpy(mime_sub, mp, sizeof(mime_sub) - 1);

            bool type_match = (strcmp(range.type, "*") == 0 || strcmp(range.type, mime_type) == 0);
            bool sub_match = (strcmp(range.subtype, "*") == 0 || strcmp(range.subtype, mime_sub) == 0);

            if (type_match && sub_match) {
                int spec = rest_media_type_specificity(supported[i]);
                if (range.quality > best_q ||
                    (range.quality == best_q && spec > best_spec)) {
                    best_q = range.quality;
                    best_spec = spec;
                    best = supported[i];
                }
            }
        }
        token = strtok(NULL, ",");
    }

    return (best_q > 0.0) ? best : supported[0];
}

char* rest_build_cors_headers(rest_cors_policy_t* policy, const char* origin,
                               char* buf, size_t len) {
    if (!policy || !buf) return NULL;
    int off = snprintf(buf, len,
        "Access-Control-Allow-Origin: %s\r\n"
        "Access-Control-Allow-Credentials: %s\r\n",
        policy->origin[0] ? policy->origin : (origin ? origin : "*"),
        policy->allow_credentials ? "true" : "false");
    if (policy->allowed_methods[0])
        off += snprintf(buf + off, len - off, "Access-Control-Allow-Methods: %s\r\n",
                        policy->allowed_methods);
    if (policy->allowed_headers[0])
        off += snprintf(buf + off, len - off, "Access-Control-Allow-Headers: %s\r\n",
                        policy->allowed_headers);
    if (policy->max_age > 0)
        off += snprintf(buf + off, len - off, "Access-Control-Max-Age: %d\r\n",
                        policy->max_age);
    return buf;
}

char* rest_build_cache_headers(rest_etag_t etag, int32_t max_age, rest_cache_control_t cc,
                                char* buf, size_t len) {
    if (!buf) return NULL;
    int off = 0;
    if (etag.value[0])
        off += snprintf(buf + off, len - off, "ETag: %s\r\n", etag.value);
    if (max_age > 0) {
        const char* cc_str = "public";
        if (cc == REST_CACHE_PRIVATE) cc_str = "private";
        else if (cc == REST_CACHE_NO_STORE) cc_str = "no-store";
        else if (cc == REST_CACHE_NO_CACHE) cc_str = "no-cache";
        off += snprintf(buf + off, len - off, "Cache-Control: %s, max-age=%d\r\n",
                        cc_str, max_age);
    }
    return buf;
}

bool rest_is_safe_method(rest_method_t m) {
    return m == REST_GET || m == REST_HEAD || m == REST_OPTIONS;
}

bool rest_is_idempotent_method(rest_method_t m) {
    return rest_is_safe_method(m) || m == REST_PUT || m == REST_DELETE;
}

bool rest_is_status_success(rest_status_t s) {
    return (int)s >= 200 && (int)s < 300;
}

bool rest_is_status_client_error(rest_status_t s) {
    return (int)s >= 400 && (int)s < 500;
}

bool rest_is_status_server_error(rest_status_t s) {
    return (int)s >= 500 && (int)s < 600;
}
