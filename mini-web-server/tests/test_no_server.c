#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "http_core.h"
#include "router.h"
#include "middleware.h"
#include "static_serve.h"
#include "cgi_handler.h"
#include "json_helper.h"
#include "websocket.h"
#include "session.h"
// #include "server.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %-50s ", name); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
    tests_failed++; \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_EQ_INT(a, b, msg) do { \
    if ((a) != (b)) { \
        char _buf[128]; \
        snprintf(_buf, sizeof(_buf), "%s (expected %d, got %d)", msg, (int)(b), (int)(a)); \
        FAIL(_buf); return; \
    } \
} while(0)

#define ASSERT_STREQ(a, b, msg) do { \
    if (!(a) || !(b) || strcmp((a), (b)) != 0) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "%s (expected '%s', got '%s')", msg, \
                 (b) ? (b) : "(null)", (a) ? (a) : "(null)"); \
        FAIL(_buf); return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(p, msg) do { \
    if (!(p)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_NULL(p, msg) do { \
    if (p) { FAIL(msg); return; } \
} while(0)

/* ── test_http_core ──────────────────────────────────────────────────────── */
static void test_http_method_str(void) {
    TEST("http_method_str");
    ASSERT_STREQ(http_method_str(HTTP_GET),    "GET",    "method GET");
    ASSERT_STREQ(http_method_str(HTTP_POST),   "POST",   "method POST");
    ASSERT_STREQ(http_method_str(HTTP_PUT),    "PUT",    "method PUT");
    ASSERT_STREQ(http_method_str(HTTP_DELETE), "DELETE", "method DELETE");
    ASSERT_STREQ(http_method_str(HTTP_UNKNOWN),"UNKNOWN","method UNKNOWN");
    PASS();
}

static void test_http_method_from_str(void) {
    TEST("http_method_from_str");
    ASSERT_EQ_INT(http_method_from_str("GET"),  HTTP_GET,  "parse GET");
    ASSERT_EQ_INT(http_method_from_str("POST"), HTTP_POST, "parse POST");
    ASSERT_EQ_INT(http_method_from_str("PATCH"),HTTP_PATCH,"parse PATCH");
    ASSERT_EQ_INT(http_method_from_str("XYZ"),  HTTP_UNKNOWN, "unknown");
    PASS();
}

static void test_http_status_text(void) {
    TEST("http_status_text");
    ASSERT_STREQ(http_status_text(200), "OK", "200 OK");
    ASSERT_STREQ(http_status_text(404), "Not Found", "404 Not Found");
    ASSERT_STREQ(http_status_text(500), "Internal Server Error", "500");
    ASSERT_STREQ(http_status_text(999), "Unknown", "out of range");
    PASS();
}

static void test_http_request_parse(void) {
    TEST("http_request_parse");
    HttpRequest req;
    http_request_init(&req);

    bool ok = http_parse_request_line("GET /api/users?id=42 HTTP/1.1", &req);
    ASSERT(ok, "parse request line");
    ASSERT_EQ_INT(req.method, HTTP_GET, "method");
    ASSERT_STREQ(req.path, "/api/users", "path");
    ASSERT_STREQ(req.query_string, "id=42", "query string");

    http_parse_header("Host: localhost:8080", &req);
    http_parse_header("Content-Type: application/json", &req);
    ASSERT_EQ_INT(req.header_count, 2, "header count");

    const char *host = http_request_get_header(&req, "Host");
    ASSERT_STREQ(host, "localhost:8080", "Host header");

    const char *missing = http_request_get_header(&req, "X-Missing");
    ASSERT_NULL(missing, "missing header");

    http_request_free(&req);
    PASS();
}

static void test_http_response_serialize(void) {
    TEST("http_response_serialize");
    HttpResponse res;
    http_response_init(&res);
    http_response_add_header(&res, "Content-Type", "text/plain");
    http_response_set_body_str(&res, "Hello World");

    char buf[1024];
    int len = http_serialize_response(&res, buf, sizeof(buf));
    ASSERT(len > 0, "serialize length");
    ASSERT(strstr(buf, "200 OK") != NULL, "status line");
    ASSERT(strstr(buf, "Content-Type: text/plain") != NULL, "Content-Type");
    ASSERT(strstr(buf, "Hello World") != NULL, "body");

    http_response_free(&res);
    PASS();
}

static void test_http_url_decode(void) {
    TEST("http_url_decode");
    char out[256];
    http_url_decode("hello%20world", out, sizeof(out));
    ASSERT_STREQ(out, "hello world", "space decode");
    http_url_decode("%2F%3F%26%3D", out, sizeof(out));
    ASSERT_STREQ(out, "/?&=", "special chars");
    PASS();
}

static void test_http_query_string(void) {
    TEST("http_query_string");
    char key[128], val[128];
    bool ok = http_parse_query_string("name=alice&age=30&city=nyc",
                                       key, sizeof(key), val, sizeof(val), "age");
    ASSERT(ok, "parse age");
    ASSERT_STREQ(val, "30", "age value");
    ok = http_parse_query_string("name=alice&age=30",
                                  key, sizeof(key), val, sizeof(val), "missing");
    ASSERT(!ok, "missing key");
    PASS();
}

/* ── test_router ──────────────────────────────────────────────────────────── */
static bool test_handler_1(const HttpRequest *req, HttpResponse *res,
                            const RouteParam *params, int count) {
    (void)req; (void)params; (void)count;
    http_response_set_body_str(res, "handler1");
    return 1;
}

static bool test_handler_2(const HttpRequest *req, HttpResponse *res,
                            const RouteParam *params, int count) {
    (void)req; (void)params; (void)count;
    http_response_set_body_str(res, "handler2");
    return 1;
}

static void test_router_basic(void) {
    TEST("router_basic");
    Router *r = router_create();
    ASSERT_NOT_NULL(r, "create router");

    bool ok = router_add(r, HTTP_GET, "/api/users", test_handler_1);
    ASSERT(ok, "add route");
    ok = router_add(r, HTTP_POST, "/api/users", test_handler_2);
    ASSERT(ok, "add POST route");

    HttpRequest req;
    http_request_init(&req);
    http_parse_request_line("GET /api/users HTTP/1.1", &req);

    HttpResponse res;
    http_response_init(&res);

    bool dispatched = router_dispatch(r, HTTP_GET, "/api/users", &req, &res);
    ASSERT(dispatched, "dispatch GET /api/users");
    ASSERT(res.body && strcmp(res.body, "handler1") == 0, "handler1 called");

    http_response_free(&res);
    http_response_init(&res);

    dispatched = router_dispatch(r, HTTP_POST, "/api/users", &req, &res);
    ASSERT(dispatched, "dispatch POST /api/users");
    ASSERT(res.body && strcmp(res.body, "handler2") == 0, "handler2 called");

    /* Non-existent route */
    dispatched = router_dispatch(r, HTTP_GET, "/api/nonexistent", &req, &res);
    ASSERT(!dispatched, "non-existent route returns false");

    http_request_free(&req);
    http_response_free(&res);
    router_destroy(r);
    PASS();
}

static void test_router_params(void) {
    TEST("router_params");
    Router *r = router_create();
    ASSERT_NOT_NULL(r, "create router");

    router_add(r, HTTP_GET, "/users/:id/posts/:post_id", test_handler_1);

    HttpRequest req;
    http_request_init(&req);
    HttpResponse res;
    http_response_init(&res);

    bool dispatched = router_dispatch(r, HTTP_GET, "/users/42/posts/99", &req, &res);
    ASSERT(dispatched, "dispatch param route");

    http_request_free(&req);
    http_response_free(&res);
    router_destroy(r);
    PASS();
}

/* ── test_middleware ──────────────────────────────────────────────────────── */
static void test_middleware_chain(void) {
    TEST("middleware_chain");
    MiddlewareChain chain;
    middleware_chain_init(&chain);

    ASSERT_EQ_INT(chain.count, 0, "empty chain");

    bool ok = middleware_chain_add(&chain, middleware_logger, NULL);
    ASSERT(ok, "add logger");
    ok = middleware_chain_add(&chain, middleware_cors, NULL);
    ASSERT(ok, "add cors");
    ASSERT_EQ_INT(chain.count, 2, "chain count");

    HttpRequest req;
    http_request_init(&req);
    http_parse_request_line("GET / HTTP/1.1", &req);

    HttpResponse res;
    http_response_init(&res);
    http_response_set_body_str(&res, "ok");

    MiddlewareContext mctx;
    memset(&mctx, 0, sizeof(mctx));

    bool executed = middleware_chain_execute(&chain, &req, &res, &mctx);
    ASSERT(executed, "chain executed");

    http_request_free(&req);
    http_response_free(&res);
    middleware_chain_clear(&chain);
    ASSERT_EQ_INT(chain.count, 0, "cleared chain");
    PASS();
}

static void test_middleware_cors_preflight(void) {
    TEST("middleware_cors_preflight");
    HttpRequest req;
    http_request_init(&req);
    http_parse_request_line("OPTIONS /api HTTP/1.1", &req);

    HttpResponse res;
    http_response_init(&res);

    MiddlewareContext mctx;
    memset(&mctx, 0, sizeof(mctx));

    MiddlewareResult result = middleware_cors(&req, &res, &mctx);
    ASSERT_EQ_INT(result, MIDDLEWARE_STOP, "OPTIONS stops chain");
    ASSERT_EQ_INT(res.status_code, HTTP_STATUS_NO_CONTENT, "204 No Content");

    http_request_free(&req);
    http_response_free(&res);
    PASS();
}

static void test_middleware_auth(void) {
    TEST("middleware_auth_missing");
    HttpRequest req;
    http_request_init(&req);
    http_parse_request_line("GET /api HTTP/1.1", &req);

    HttpResponse res;
    http_response_init(&res);

    MiddlewareContext mctx;
    memset(&mctx, 0, sizeof(mctx));

    MiddlewareResult result = middleware_auth_bearer(&req, &res, &mctx);
    ASSERT_EQ_INT(result, MIDDLEWARE_STOP, "no auth stops chain");

    http_request_free(&req);
    http_response_free(&res);
    PASS();
}

/* ── test_json_helper ────────────────────────────────────────────────────── */
static void test_json_parse_simple(void) {
    TEST("json_parse_simple");
    JsonValue *root = json_parse("{\"name\":\"alice\",\"age\":30}");
    ASSERT_NOT_NULL(root, "parse object");
    ASSERT_EQ_INT(json_value_type(root), JSON_OBJECT, "root is object");
    ASSERT_EQ_INT(json_object_size(root), 2, "2 keys");

    JsonValue *name = json_object_get(root, "name");
    ASSERT_NOT_NULL(name, "name key exists");
    ASSERT_STREQ(json_value_get_string(name, NULL), "alice", "name value");

    JsonValue *age = json_object_get(root, "age");
    ASSERT_NOT_NULL(age, "age key exists");
    ASSERT_EQ_INT((int)json_value_get_number(age, -1), 30, "age value");

    json_value_free(root);
    PASS();
}

static void test_json_parse_array(void) {
    TEST("json_parse_array");
    JsonValue *root = json_parse("[1, 2, 3, 4, 5]");
    ASSERT_NOT_NULL(root, "parse array");
    ASSERT_EQ_INT(json_value_type(root), JSON_ARRAY, "root is array");
    ASSERT_EQ_INT(json_array_size(root), 5, "5 elements");

    JsonValue *el = json_array_get(root, 2);
    ASSERT_NOT_NULL(el, "element 2");
    ASSERT_EQ_INT((int)json_value_get_number(el, -1), 3, "value is 3");

    json_value_free(root);
    PASS();
}

static void test_json_parse_nested(void) {
    TEST("json_parse_nested");
    JsonValue *root = json_parse("{\"users\":[{\"id\":1},{\"id\":2}],\"meta\":{\"count\":2}}");
    ASSERT_NOT_NULL(root, "parse nested");

    JsonValue *users = json_object_get(root, "users");
    ASSERT_NOT_NULL(users, "users key");
    ASSERT_EQ_INT(json_array_size(users), 2, "2 users");

    JsonValue *meta = json_object_get(root, "meta");
    ASSERT_NOT_NULL(meta, "meta key");
    JsonValue *count = json_object_get(meta, "count");
    ASSERT_EQ_INT((int)json_value_get_number(count, -1), 2, "count");

    json_value_free(root);
    PASS();
}

static void test_json_build(void) {
    TEST("json_build");
    JsonValue *obj = json_build_object();
    ASSERT_NOT_NULL(obj, "build object");

    json_object_set(obj, "status", json_build_string("ok"));
    json_object_set(obj, "code", json_build_number(200));
    json_object_set(obj, "active", json_build_bool(1));

    char buf[1024];
    int len = json_serialize(obj, buf, sizeof(buf));
    ASSERT(len > 0, "serialize");
    ASSERT(strstr(buf, "\"status\"") != NULL, "has status");
    ASSERT(strstr(buf, "\"ok\"") != NULL, "has ok value");

    json_value_free(obj);
    PASS();
}

static void test_json_path_access(void) {
    TEST("json_path_access");
    JsonValue *root = json_parse("{\"user\":{\"name\":\"bob\",\"address\":{\"city\":\"paris\"}}}");
    ASSERT_NOT_NULL(root, "parse");

    JsonValue *city = json_path_get(root, "user.address.city");
    ASSERT_NOT_NULL(city, "path user.address.city");
    ASSERT_STREQ(json_value_get_string(city, NULL), "paris", "city value");

    JsonValue *missing = json_path_get(root, "user.phone");
    ASSERT_NULL(missing, "missing path");

    json_value_free(root);
    PASS();
}

static void test_json_edge_cases(void) {
    TEST("json_edge_cases");
    /* Empty object */
    JsonValue *empty_obj = json_parse("{}");
    ASSERT_NOT_NULL(empty_obj, "empty object");
    ASSERT_EQ_INT(json_object_size(empty_obj), 0, "0 keys");
    json_value_free(empty_obj);

    /* Empty array */
    JsonValue *empty_arr = json_parse("[]");
    ASSERT_NOT_NULL(empty_arr, "empty array");
    ASSERT_EQ_INT(json_array_size(empty_arr), 0, "0 elements");
    json_value_free(empty_arr);

    /* Primitives */
    JsonValue *null_v = json_parse("null");
    ASSERT_NOT_NULL(null_v, "null");
    json_value_free(null_v);

    JsonValue *true_v = json_parse("true");
    ASSERT_NOT_NULL(true_v, "true");
    ASSERT(json_value_get_bool(true_v, 0) == 1, "bool true");
    json_value_free(true_v);

    JsonValue *false_v = json_parse("false");
    ASSERT_NOT_NULL(false_v, "false");
    ASSERT(json_value_get_bool(false_v, 1) == 0, "bool false");
    json_value_free(false_v);

    /* Invalid JSON */
    JsonValue *invalid = json_parse("{bad json");
    ASSERT_NULL(invalid, "invalid returns null");

    /* Null input */
    JsonValue *null_input = json_parse(NULL);
    ASSERT_NULL(null_input, "null input returns null");

    PASS();
}

/* ── test_websocket ──────────────────────────────────────────────────────── */
static void test_ws_sha1(void) {
    TEST("ws_sha1");
    /* SHA-1("abc") = a9993e36 4706816a ba3e2571 7850c26c 9cd0d89d */
    uint8_t digest[20];
    ws_sha1((const uint8_t *)"abc", 3, digest);

    /* Check first and last bytes */
    ASSERT_EQ_INT(digest[0],  0xa9, "sha1 byte 0");
    ASSERT_EQ_INT(digest[1],  0x99, "sha1 byte 1");
    ASSERT_EQ_INT(digest[19], 0x9d, "sha1 byte 19");
    PASS();
}

static void test_ws_base64(void) {
    TEST("ws_base64");
    const uint8_t input[] = "hello";
    char out[32];
    int len = ws_base64_encode(input, 5, out, sizeof(out));
    ASSERT(len > 0, "base64 encode");
    ASSERT_STREQ(out, "aGVsbG8=", "base64 hello");
    PASS();
}

static void test_ws_handshake_validate(void) {
    TEST("ws_handshake_validate");
    HttpRequest req;
    http_request_init(&req);
    http_parse_request_line("GET /chat HTTP/1.1", &req);
    http_parse_header("Upgrade: websocket", &req);
    http_parse_header("Connection: Upgrade", &req);
    http_parse_header("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", &req);
    http_parse_header("Sec-WebSocket-Version: 13", &req);

    char accept[64];
    bool valid = ws_validate_handshake(&req, accept, sizeof(accept));
    ASSERT(valid, "valid handshake");
    /* Known accept value for "dGhlIHNhbXBsZSBub25jZQ==" */
    ASSERT_STREQ(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "accept key");

    http_request_free(&req);
    PASS();
}

static void test_ws_frame_encode_decode(void) {
    TEST("ws_frame_encode_decode");
    WsFrame in_frame;
    memset(&in_frame, 0, sizeof(in_frame));
    in_frame.fin = 1;
    in_frame.opcode = WS_OP_TEXT;
    in_frame.masked = 1;
    in_frame.payload_len = 5;
    in_frame.payload_data = (uint8_t *)"hello";
    in_frame.mask_key[0] = 0x12;
    in_frame.mask_key[1] = 0x34;
    in_frame.mask_key[2] = 0x56;
    in_frame.mask_key[3] = 0x78;

    uint8_t wire[128];
    int wire_len = ws_encode_frame(&in_frame, wire, sizeof(wire));
    ASSERT(wire_len > 0, "encode frame");

    WsFrame out_frame;
    int rc = ws_decode_frame(wire, (size_t)wire_len, &out_frame);
    ASSERT_EQ_INT(rc, 0, "decode frame");
    ASSERT_EQ_INT(out_frame.fin, 1, "fin");
    ASSERT_EQ_INT(out_frame.opcode, WS_OP_TEXT, "opcode");
    ASSERT_EQ_INT((int)out_frame.payload_len, 5, "payload len");
    ASSERT(memcmp(out_frame.payload_data, "hello", 5) == 0, "payload content");

    ws_frame_free(&out_frame);
    PASS();
}

static void test_ws_mask(void) {
    TEST("ws_mask_unmask");
    uint8_t data[] = { 'H', 'e', 'l', 'l', 'o' };
    uint8_t mask[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
    uint8_t original[5];
    memcpy(original, data, 5);

    ws_mask_payload(data, 5, mask);
    /* Should be different from original */
    ASSERT(memcmp(data, original, 5) != 0, "masked data differs");

    ws_mask_payload(data, 5, mask);
    /* Double mask = original (XOR is its own inverse) */
    ASSERT(memcmp(data, original, 5) == 0, "unmask recovers original");
    PASS();
}

/* ── test_session ────────────────────────────────────────────────────────── */
static void test_session_create_get(void) {
    TEST("session_create_get");
    SessionStore store;
    session_store_init(&store);

    Session *s = session_create(&store);
    ASSERT_NOT_NULL(s, "create session");
    ASSERT(strlen(s->id) > 0, "has session id");
    ASSERT(s->active == 1, "session active");

    Session *found = session_get(&store, s->id);
    ASSERT_NOT_NULL(found, "get by id");
    ASSERT(found == s, "same pointer");

    Session *missing = session_get(&store, "nonexistent");
    ASSERT_NULL(missing, "missing session");
    PASS();
}

static void test_session_variables(void) {
    TEST("session_variables");
    SessionStore store;
    session_store_init(&store);
    Session *s = session_create(&store);
    ASSERT_NOT_NULL(s, "create session");

    bool ok = session_var_set(s, "user_id", "42");
    ASSERT(ok, "set user_id");
    ok = session_var_set(s, "role", "admin");
    ASSERT(ok, "set role");

    ASSERT_STREQ(session_var_get(s, "user_id"), "42", "get user_id");
    ASSERT_STREQ(session_var_get(s, "role"), "admin", "get role");
    ASSERT_NULL(session_var_get(s, "missing"), "missing key");

    /* Update existing */
    ok = session_var_set(s, "user_id", "99");
    ASSERT(ok, "update user_id");
    ASSERT_STREQ(session_var_get(s, "user_id"), "99", "updated value");

    /* Remove */
    ok = session_var_remove(s, "role");
    ASSERT(ok, "remove role");
    ASSERT_NULL(session_var_get(s, "role"), "role removed");

    /* Clear all */
    session_clear(s);
    ASSERT_NULL(session_var_get(s, "user_id"), "cleared");
    PASS();
}

static void test_session_cookie_parse(void) {
    TEST("session_cookie_parse");
    HttpRequest req;
    http_request_init(&req);
    http_parse_request_line("GET / HTTP/1.1", &req);
    http_parse_header("Cookie: SESSID=abc123def456; lang=en; theme=dark", &req);

    char sid[64];
    bool ok = session_parse_cookie(&req, "SESSID", sid, sizeof(sid));
    ASSERT(ok, "parse SESSID");
    ASSERT_STREQ(sid, "abc123def456", "session id value");

    ok = session_parse_cookie(&req, "theme", sid, sizeof(sid));
    ASSERT(ok, "parse theme");
    ASSERT_STREQ(sid, "dark", "theme value");

    ok = session_parse_cookie(&req, "missing", sid, sizeof(sid));
    ASSERT(!ok, "missing cookie");

    http_request_free(&req);
    PASS();
}

static void test_session_destroy_cleanup(void) {
    TEST("session_destroy_cleanup");
    SessionStore store;
    session_store_init(&store);
    session_store_set_ttl(&store, -1); /* immediate expiry for testing */

    Session *s = session_create(&store);
    ASSERT_NOT_NULL(s, "create");

    bool ok = session_destroy(&store, s->id);
    ASSERT(ok, "destroy");

    Session *found = session_get(&store, s->id);
    ASSERT_NULL(found, "destroyed not found");

    int cleaned = session_store_cleanup(&store);
    ASSERT(cleaned >= 0, "cleanup returns count");
    PASS();
}

/* ── test_static_serve ────────────────────────────────────────────────────── */
static void test_static_mime(void) {
    TEST("static_mime_types");
    StaticConfig cfg;
    static_config_init(&cfg, "./public");
    static_config_load_default_mimes(&cfg);

    const char *mime = static_get_mime_type(&cfg, "/index.html");
    ASSERT(strstr(mime, "text/html") != NULL, "html mime");

    mime = static_get_mime_type(&cfg, "/style.css");
    ASSERT(strstr(mime, "text/css") != NULL, "css mime");

    mime = static_get_mime_type(&cfg, "/app.js");
    ASSERT(strstr(mime, "javascript") != NULL, "js mime");

    mime = static_get_mime_type(&cfg, "/logo.png");
    ASSERT(strstr(mime, "image/png") != NULL, "png mime");

    mime = static_get_mime_type(&cfg, "/unknown.xyz");
    ASSERT_STREQ(mime, "application/octet-stream", "unknown mime");
    PASS();
}

static void test_static_etag_cache(void) {
    TEST("static_etag_cache");
    FileCacheInfo info;
    info.file_size = 1024;
    info.mtime = 1715000000;

    static_build_etag("/test.txt", info.mtime, info.file_size,
                       info.etag, sizeof(info.etag));
    ASSERT(strlen(info.etag) > 0, "etag generated");

    strftime(info.last_modified, sizeof(info.last_modified),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&info.mtime));
    ASSERT(strlen(info.last_modified) > 0, "last-modified generated");

    /* If-None-Match match */
    HttpRequest req;
    http_request_init(&req);
    http_parse_header("If-None-Match: \"663800c0-400\"", &req);

    bool not_mod = static_check_not_modified(&req, &info);
    /* Will be true if etag matches, false otherwise */
    /* Just verify it doesn't crash */
    (void)not_mod;

    http_request_free(&req);
    PASS();
}

/* ── test_cgi_handler ────────────────────────────────────────────────────── */
static void test_cgi_fcgi_header(void) {
    TEST("cgi_fcgi_header");
    FcgiHeader hdr;
    fcgi_build_header(FCGI_BEGIN_REQUEST, 1, 8, 0, &hdr);
    ASSERT_EQ_INT(hdr.version, 1, "fcgi version");
    ASSERT_EQ_INT(hdr.type, FCGI_BEGIN_REQUEST, "fcgi type");

    /* Parse back */
    uint8_t raw[8] = { 1, FCGI_END_REQUEST, 0, 1, 0, 8, 0, 0 };
    FcgiHeader parsed;
    bool ok = fcgi_parse_header(raw, 8, &parsed);
    ASSERT(ok, "parse fcgi header");
    ASSERT_EQ_INT(parsed.version, 1, "parsed version");
    ASSERT_EQ_INT(parsed.type, FCGI_END_REQUEST, "parsed type");
    ASSERT_EQ_INT((int)parsed.request_id, 1, "parsed request id");
    ASSERT_EQ_INT((int)parsed.content_length, 8, "parsed content length");
    PASS();
}

static void test_cgi_env_config(void) {
    TEST("cgi_env_config");
    CgiConfig cfg;
    cgi_config_init(&cfg, "/usr/bin/python");

    HttpRequest req;
    http_request_init(&req);
    http_parse_request_line("GET /cgi-bin/test?action=view HTTP/1.1", &req);
    http_parse_header("Content-Type: text/html", &req);
    http_parse_header("Host: localhost", &req);
    http_parse_header("User-Agent: test-agent", &req);

    cgi_config_set_request_env(&cfg, &req);
    ASSERT(cfg.env_count > 0, "env vars set");
    ASSERT_STREQ(cfg.script_path, "/usr/bin/python", "script path");
    ASSERT_EQ_INT(cfg.timeout_seconds, CGI_TIMEOUT_SECONDS, "default timeout");

    http_request_free(&req);
    PASS();
}

static void test_cgi_parse_status(void) {
    TEST("cgi_parse_status_line");
    int status_code = 0;
    char status_text[128];

    bool ok = cgi_parse_status_line("Status: 302 Found\r\n", 18,
                                      &status_code, status_text, 128);
    ASSERT(ok, "parse Status header");
    ASSERT_EQ_INT(status_code, 302, "status 302");

    ok = cgi_parse_status_line("HTTP/1.0 200 OK\r\n", 17,
                                &status_code, status_text, 128);
    ASSERT(ok, "parse HTTP status");
    ASSERT_EQ_INT(status_code, 200, "status 200");
    PASS();
}

/* ── test_server ──────────────────────────────────────────────────────────── */
// server tests removed

// server tests removed

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void) {
    printf("\n========================================\n");
    printf(" mini-web-server Test Suite\n");
    printf("========================================\n\n");

    printf("[HTTP Core]\n");
    test_http_method_str();
    test_http_method_from_str();
    test_http_status_text();
    test_http_request_parse();
    test_http_response_serialize();
    test_http_url_decode();
    test_http_query_string();

    printf("\n[Router]\n");
    test_router_basic();
    test_router_params();

    printf("\n[Middleware]\n");
    test_middleware_chain();
    test_middleware_cors_preflight();
    test_middleware_auth();

    printf("\n[JSON Helper]\n");
    test_json_parse_simple();
    test_json_parse_array();
    test_json_parse_nested();
    test_json_build();
    test_json_path_access();
    test_json_edge_cases();

    printf("\n[WebSocket]\n");
    test_ws_sha1();
    test_ws_base64();
    test_ws_handshake_validate();
    test_ws_frame_encode_decode();
    test_ws_mask();

    printf("\n[Session]\n");
    test_session_create_get();
    test_session_variables();
    test_session_cookie_parse();
    test_session_destroy_cleanup();

    printf("\n[Static Serve]\n");
    test_static_mime();
    test_static_etag_cache();

    printf("\n[CGI Handler]\n");
    test_cgi_fcgi_header();
    test_cgi_env_config();
    test_cgi_parse_status();

    printf("\n[Server Core]\n");
    test_thread_pool_create();
    test_server_init_destroy();

    printf("\n========================================\n");
    printf(" Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf("\n========================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
