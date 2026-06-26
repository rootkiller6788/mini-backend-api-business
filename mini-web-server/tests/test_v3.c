#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "http_core.h"
#include "router.h"
#include "middleware.h"
#include "json_helper.h"
#include "websocket.h"
#include "session.h"
#include "static_serve.h"
#include "cgi_handler.h"

static int g_passed = 0;
static int g_failed = 0;
static int g_total = 0;

#define CHECK(cond, name) do { \
    g_total++; \
    if (cond) { \
        printf("  PASS %s\n", name); \
        g_passed++; \
    } else { \
        printf("  FAIL %s\n", name); \
        g_failed++; \
    } \
} while(0)

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\n========== mini-web-server Test Suite ==========\n\n");

    printf("[HTTP Core]\n");
    CHECK(strcmp(http_method_str(HTTP_GET), "GET") == 0, "method_str GET");
    CHECK(http_method_from_str("POST") == HTTP_POST, "method_from_str");
    CHECK(http_method_from_str("XYZ") == HTTP_UNKNOWN, "unknown method");
    CHECK(strcmp(http_status_text(200), "OK") == 0, "status 200");
    CHECK(strcmp(http_status_text(404), "Not Found") == 0, "status 404");

    {
        HttpRequest req;
        http_request_init(&req);
        CHECK(http_parse_request_line("GET /api/users?id=42 HTTP/1.1", &req), "parse req line");
        CHECK(req.method == HTTP_GET, "method GET");
        http_parse_header("Host: localhost", &req);
        http_parse_header("Content-Type: application/json", &req);
        CHECK(req.header_count == 2, "header count");
        CHECK(strcmp(http_request_get_header(&req, "Host"), "localhost") == 0, "get header");
        CHECK(http_request_get_header(&req, "Missing") == NULL, "missing header");
        http_request_free(&req);
    }
    {
        char out[256];
        http_url_decode("hello%20world", out, sizeof(out));
        CHECK(strcmp(out, "hello world") == 0, "url decode");
    }
    {
        HttpResponse res; http_response_init(&res);
        http_response_add_header(&res, "X-Test", "123");
        http_response_set_body_str(&res, "hello");
        char buf[1024];
        int len = http_serialize_response(&res, buf, sizeof(buf));
        CHECK(len > 0 && strstr(buf, "hello") != NULL, "serialize");
        http_response_free(&res);
    }

    printf("\n[Router]\n");
    {
        Router *r = router_create();
        CHECK(r != NULL, "router create");
        router_add(r, HTTP_GET, "/test", NULL);
        router_destroy(r);
    }
    {
        Router *r = router_create();
        router_add(r, HTTP_GET, "/users/:id", NULL);
        HttpRequest req; http_request_init(&req);
        http_parse_request_line("GET /users/42 HTTP/1.1", &req);
        HttpResponse res; http_response_init(&res);
        CHECK(router_dispatch(r, HTTP_GET, "/users/42", &req, &res), "param route");
        http_request_free(&req); http_response_free(&res);
        router_destroy(r);
    }

    printf("\n[Middleware]\n");
    {
        MiddlewareChain chain; middleware_chain_init(&chain);
        middleware_chain_add(&chain, middleware_logger, NULL);
        middleware_chain_add(&chain, middleware_cors, NULL);
        CHECK(chain.count == 2, "chain add");
        HttpRequest req; http_request_init(&req);
        http_parse_request_line("GET / HTTP/1.1", &req);
        HttpResponse res; http_response_init(&res);
        MiddlewareContext mctx; memset(&mctx, 0, sizeof(mctx));
        CHECK(middleware_chain_execute(&chain, &req, &res, &mctx), "chain exec");
        http_request_free(&req); http_response_free(&res);
        middleware_chain_clear(&chain);
    }
    {
        HttpRequest req; http_request_init(&req);
        http_parse_request_line("OPTIONS /api HTTP/1.1", &req);
        HttpResponse res; http_response_init(&res);
        MiddlewareContext mctx; memset(&mctx, 0, sizeof(mctx));
        CHECK(middleware_cors(&req, &res, &mctx) == MIDDLEWARE_STOP, "cors OPTIONS");
        http_request_free(&req); http_response_free(&res);
    }

    printf("\n[JSON Helper]\n");
    {
        JsonValue *v = json_parse("{\"name\":\"alice\",\"age\":30}");
        CHECK(v != NULL, "parse object");
        CHECK(json_object_size(v) == 2, "obj size");
        JsonValue *age = json_object_get(v, "age");
        CHECK(age != NULL && (int)json_value_get_number(age, -1) == 30, "age value");
        json_value_free(v);
    }
    {
        JsonValue *v = json_parse("[10, 20, 30]");
        CHECK(v != NULL && json_array_size(v) == 3, "parse array");
        json_value_free(v);
    }
    CHECK(json_parse("null") != NULL, "parse null");
    CHECK(json_parse("bad{") == NULL, "invalid returns NULL");
    {
        JsonValue *obj = json_build_object();
        json_object_set(obj, "key", json_build_string("val"));
        char buf[256];
        CHECK(json_serialize(obj, buf, sizeof(buf)) > 0, "serialize json");
        json_value_free(obj);
    }
    {
        JsonValue *root = json_parse("{\"a\":{\"b\":{\"c\":123}}}");
        JsonValue *c = json_path_get(root, "a.b.c");
        CHECK(c != NULL, "path access");
        json_value_free(root);
    }

    printf("\n[WebSocket]\n");
    {
        uint8_t d[20]; ws_sha1((const uint8_t*)"abc", 3, d);
        CHECK(d[0] == 0xa9, "SHA-1");
    }
    {
        char out[32];
        ws_base64_encode((const uint8_t*)"hello", 5, out, sizeof(out));
        CHECK(strcmp(out, "aGVsbG8=") == 0, "base64");
    }
    {
        HttpRequest req; http_request_init(&req);
        http_parse_request_line("GET /chat HTTP/1.1", &req);
        http_parse_header("Upgrade: websocket", &req);
        http_parse_header("Connection: Upgrade", &req);
        http_parse_header("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", &req);
        http_parse_header("Sec-WebSocket-Version: 13", &req);
        char accept[64];
        CHECK(ws_validate_handshake(&req, accept, sizeof(accept)), "ws handshake");
        http_request_free(&req);
    }
    {
        WsFrame f; memset(&f, 0, sizeof(f));
        f.fin = 1; f.opcode = WS_OP_TEXT; f.masked = 1;
        f.payload_data = (uint8_t*)"ok"; f.payload_len = 2;
        uint8_t mk[4] = {1,2,3,4}; memcpy(f.mask_key, mk, 4);
        uint8_t w[128];
        int wl = ws_encode_frame(&f, w, sizeof(w));
        WsFrame d;
        CHECK(ws_decode_frame(w, (size_t)wl, &d) == 0, "frame roundtrip");
        ws_frame_free(&d);
    }

    printf("\n[Session]\n");
    {
        SessionStore store; session_store_init(&store);
        Session *s = session_create(&store);
        CHECK(s != NULL && strlen(s->id) > 0, "create session");
        CHECK(session_get(&store, s->id) == s, "get session");
    }
    {
        SessionStore store; session_store_init(&store);
        Session *s = session_create(&store);
        session_var_set(s, "key", "val");
        CHECK(strcmp(session_var_get(s, "key"), "val") == 0, "var get/set");
    }
    {
        HttpRequest req; http_request_init(&req);
        http_parse_header("Cookie: SID=abc123; theme=dark", &req);
        char sid[64];
        CHECK(session_parse_cookie(&req, "SID", sid, sizeof(sid)), "parse cookie");
        CHECK(strcmp(sid, "abc123") == 0, "cookie val");
        http_request_free(&req);
    }

    printf("\n[Static Serve]\n");
    {
        StaticConfig cfg; static_config_init(&cfg, ".");
        static_config_load_default_mimes(&cfg);
        const char *m = static_get_mime_type(&cfg, "/x.html");
        CHECK(m != NULL && strstr(m, "text/html") != NULL, "html mime");
    }
    {
        FileCacheInfo info; info.file_size = 100; info.mtime = time(NULL);
        static_build_etag("/f", info.mtime, info.file_size, info.etag, sizeof(info.etag));
        CHECK(strlen(info.etag) > 0, "etag");
    }

    printf("\n[CGI Handler]\n");
    {
        CgiConfig cfg; cgi_config_init(&cfg, "/bin/echo");
        CHECK(strcmp(cfg.script_path, "/bin/echo") == 0, "cgi init");
    }
    {
        FcgiHeader hdr; fcgi_build_header(FCGI_BEGIN_REQUEST, 1, 8, 0, &hdr);
        CHECK(hdr.version == 1 && hdr.type == FCGI_BEGIN_REQUEST, "fcgi header");
    }

    printf("\n========================================\n");
    printf(" Results: %d/%d passed", g_passed, g_total);
    if (g_failed > 0) printf(", %d FAILED", g_failed);
    printf("\n========================================\n\n");

    return g_failed > 0 ? 1 : 0;
}
