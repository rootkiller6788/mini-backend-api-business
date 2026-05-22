#include "rest_design.h"
#include <string.h>
#include <stdio.h>

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
}

void rest_router_register(rest_router_t* r, rest_resource_t* resource) {
    (void)r;
    (void)resource;
}

const char* rest_router_resolve(rest_router_t* r, const char* uri, rest_method_t method) {
    (void)r;
    (void)uri;
    (void)method;
    return NULL;
}
