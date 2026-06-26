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

static int g_pass = 0;
static int g_fail = 0;

#define T(n) printf("  TEST %-45s ", n)
#define OK() do { printf("PASS\n"); g_pass++; } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); g_fail++; } while(0)
#define CHK(c,n) do { T(n); if (c) OK(); else FAIL(#c); } while(0)

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\n========== mini-web-server Test Suite ==========\n\n");

    printf("[HTTP Core]\n");
    CHK(strcmp(http_method_str(HTTP_GET), "GET") == 0, "method_str GET");
    CHK(http_method_from_str("POST") == HTTP_POST, "method_from_str POST");
    CHK(http_method_from_str("XYZ") == HTTP_UNKNOWN, "unknown method");
    CHK(strcmp(http_status_text(200), "OK") == 0, "status_text 200");
    CHK(strcmp(http_status_text(404), "Not Found") == 0, "status_text 404");

    {
        HttpRequest *req = calloc(1, sizeof(HttpRequest));
        http_request_init(req);
        CHK(http_parse_request_line("GET /api/users?id=42 HTTP/1.1", req), "parse req line");
        CHK(req->method == HTTP_GET, "method GET");
        http_parse_header("Host: localhost", req);
        http_parse_header("Content-Type: application/json", req);
        CHK(req->header_count == 2, "header count 2");
        CHK(strcmp(http_request_get_header(req, "Host"), "localhost") == 0, "get Host");
        CHK(http_request_get_header(req, "Missing") == NULL, "missing NULL");
        http_request_free(req); free(req);
    }

    {
        char out[256];
        http_url_decode("hello%20world", out, sizeof(out));
        CHK(strcmp(out, "hello world") == 0, "url decode");
    }

    {
        HttpResponse *res = calloc(1, sizeof(HttpResponse));
        http_response_init(res);
        http_response_set_body_str(res, "hello");
        char buf[1024];
        int len = http_serialize_response(res, buf, sizeof(buf));
        CHK(len > 0, "serialize");
        CHK(strstr(buf, "hello") != NULL, "body in output");
        http_response_free(res); free(res);
    }

    printf("\n[Router]\n");
    { Router *r = router_create(); CHK(r != NULL, "router create"); router_destroy(r); }
    {
        Router *r = router_create();
        router_add(r, HTTP_GET, "/users/:id", NULL);
        HttpRequest *req = calloc(1, sizeof(HttpRequest)); http_request_init(req);
        http_parse_request_line("GET /users/42 HTTP/1.1", req);
        HttpResponse *res = calloc(1, sizeof(HttpResponse)); http_response_init(res);
        CHK(router_dispatch(r, HTTP_GET, "/users/42", req, res), "param route dispatch");
        http_request_free(req); free(req); http_response_free(res); free(res);
        router_destroy(r);
    }

    printf("\n[Middleware]\n");
    {
        MiddlewareChain chain; middleware_chain_init(&chain);
        middleware_chain_add(&chain, middleware_logger, NULL);
        middleware_chain_add(&chain, middleware_cors, NULL);
        CHK(chain.count == 2, "chain add");
        HttpRequest *req = calloc(1, sizeof(HttpRequest)); http_request_init(req);
        http_parse_request_line("GET / HTTP/1.1", req);
        HttpResponse *res = calloc(1, sizeof(HttpResponse)); http_response_init(res);
        MiddlewareContext mctx; memset(&mctx, 0, sizeof(mctx));
        CHK(middleware_chain_execute(&chain, req, res, &mctx), "chain execute");
        http_request_free(req); free(req); http_response_free(res); free(res);
        middleware_chain_clear(&chain);
    }
    {
        HttpRequest *req = calloc(1, sizeof(HttpRequest)); http_request_init(req);
        http_parse_request_line("OPTIONS /api HTTP/1.1", req);
        HttpResponse *res = calloc(1, sizeof(HttpResponse)); http_response_init(res);
        MiddlewareContext mctx; memset(&mctx, 0, sizeof(mctx));
        CHK(middleware_cors(req, res, &mctx) == MIDDLEWARE_STOP, "cors preflight");
        http_request_free(req); free(req); http_response_free(res); free(res);
    }

    printf("\n[JSON Helper]\n");
    {
        JsonValue *v = json_parse("{\"name\":\"alice\",\"age\":30}");
        CHK(v != NULL, "parse object");
        CHK(json_object_size(v) == 2, "obj size 2");
        JsonValue *a = json_object_get(v, "age");
        CHK(a != NULL && (int)json_value_get_number(a, -1) == 30, "age=30");
        json_value_free(v);
    }
    CHK(json_parse("null") != NULL, "parse null");
    CHK(json_parse("true") != NULL, "parse true");
    CHK(json_parse("false") != NULL, "parse false");
    CHK(json_parse("bad{") == NULL, "invalid NULL");
    {
        JsonValue *arr = json_parse("[10, 20, 30]");
        CHK(arr != NULL && json_array_size(arr) == 3, "parse array 3");
        json_value_free(arr);
    }
    {
        JsonValue *obj = json_build_object();
        json_object_set(obj, "key", json_build_string("val"));
        char buf[256];
        CHK(json_serialize(obj, buf, sizeof(buf)) > 0, "json serialize");
        json_value_free(obj);
    }
    {
        JsonValue *root = json_parse("{\"a\":{\"b\":{\"c\":123}}}");
        JsonValue *c = json_path_get(root, "a.b.c");
        CHK(c != NULL && (int)json_value_get_number(c, -1) == 123, "path a.b.c=123");
        json_value_free(root);
    }

    printf("\n[WebSocket]\n");
    { uint8_t d[20]; ws_sha1((const uint8_t*)"abc", 3, d);
      CHK(d[0] == 0xa9 && d[19] == 0x9d, "SHA-1 abc"); }
    {
        char out[32];
        ws_base64_encode((const uint8_t*)"hello", 5, out, sizeof(out));
        CHK(strcmp(out, "aGVsbG8=") == 0, "base64 hello");
    }
    {
        HttpRequest *req = calloc(1, sizeof(HttpRequest)); http_request_init(req);
        http_parse_request_line("GET /chat HTTP/1.1", req);
        http_parse_header("Upgrade: websocket", req);
        http_parse_header("Connection: Upgrade", req);
        http_parse_header("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", req);
        http_parse_header("Sec-WebSocket-Version: 13", req);
        char accept[64];
        CHK(ws_validate_handshake(req, accept, sizeof(accept)), "ws handshake valid");
        CHK(strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0, "ws accept key");
        http_request_free(req); free(req);
    }
    {
        WsFrame f; memset(&f, 0, sizeof(f));
        f.fin = 1; f.opcode = WS_OP_TEXT; f.masked = 1;
        f.payload_data = (uint8_t*)"ok"; f.payload_len = 2;
        uint8_t mk[4] = {1,2,3,4}; memcpy(f.mask_key, mk, 4);
        uint8_t w[128];
        int wl = ws_encode_frame(&f, w, sizeof(w));
        WsFrame d;
        CHK(ws_decode_frame(w, (size_t)wl, &d) == 0, "ws frame roundtrip");
        CHK(d.payload_len == 2, "ws payload len 2");
        ws_frame_free(&d);
    }

    printf("\n[Session]\n");
    {
        SessionStore store; session_store_init(&store);
        Session *s = session_create(&store);
        CHK(s != NULL && strlen(s->id) > 0, "session create");
        CHK(session_get(&store, s->id) == s, "session get");
    }
    {
        SessionStore store; session_store_init(&store);
        Session *s = session_create(&store);
        session_var_set(s, "user", "admin");
        CHK(strcmp(session_var_get(s, "user"), "admin") == 0, "var get/set");
    }
    {
        HttpRequest *req = calloc(1, sizeof(HttpRequest)); http_request_init(req);
        http_parse_header("Cookie: SID=abc123; lang=en", req);
        char sid[64];
        CHK(session_parse_cookie(req, "SID", sid, sizeof(sid)), "parse cookie");
        CHK(strcmp(sid, "abc123") == 0, "cookie val");
        http_request_free(req); free(req);
    }

    printf("\n[Static Serve]\n");
    {
        StaticConfig cfg; static_config_init(&cfg, ".");
        static_config_load_default_mimes(&cfg);
        const char *m = static_get_mime_type(&cfg, "/x.html");
        CHK(m != NULL && strstr(m, "text/html") != NULL, "html mime");
        m = static_get_mime_type(&cfg, "/x.xyz");
        CHK(strcmp(m, "application/octet-stream") == 0, "unknown mime");
    }
    {
        FileCacheInfo info; info.file_size = 100; info.mtime = time(NULL);
        static_build_etag("/f", info.mtime, info.file_size, info.etag, sizeof(info.etag));
        CHK(strlen(info.etag) > 0, "etag");
    }

    printf("\n[CGI Handler]\n");
    {
        CgiConfig cfg; cgi_config_init(&cfg, "/bin/echo");
        CHK(strcmp(cfg.script_path, "/bin/echo") == 0, "cgi init");
    }
    {
        FcgiHeader hdr; fcgi_build_header(FCGI_BEGIN_REQUEST, 1, 8, 0, &hdr);
        CHK(hdr.version == 1 && hdr.type == FCGI_BEGIN_REQUEST, "fcgi header");
    }

    printf("\n========================================\n");
    printf(" Results: %d/%d passed", g_pass, g_pass + g_fail);
    if (g_fail > 0) printf(", %d FAILED", g_fail);
    printf("\n========================================\n\n");
    return g_fail > 0 ? 1 : 0;
}
