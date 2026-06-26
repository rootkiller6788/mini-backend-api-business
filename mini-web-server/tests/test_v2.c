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

static int passed = 0, failed = 0;

#define T(name, body) do { \
    printf("  %-45s ", name); fflush(stdout); \
    body \
    printf("PASS\n"); passed++; \
} while(0)

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\n=== mini-web-server Test Suite ===\n\n");

    printf("[HTTP Core]\n");
    T("http_method_str", {
        if (strcmp(http_method_str(HTTP_GET), "GET") != 0) { printf("FAIL\n"); failed++; return; }
    });
    T("http_method_from_str", {
        if (http_method_from_str("POST") != HTTP_POST) { printf("FAIL\n"); failed++; return; }
    });
    T("http_status_text", {
        if (strcmp(http_status_text(404), "Not Found") != 0) { printf("FAIL\n"); failed++; return; }
    });

    T("http_request_parse", {
        HttpRequest req;
        http_request_init(&req);
        if (!http_parse_request_line("GET /api/test HTTP/1.1", &req)) { printf("FAIL\n"); failed++; http_request_free(&req); return; }
        http_request_free(&req);
    });

    T("http_url_decode", {
        char out[256];
        http_url_decode("hello%20world", out, sizeof(out));
        if (strcmp(out, "hello world") != 0) { printf("FAIL\n"); failed++; return; }
    });

    printf("\n[Router]\n");
    T("router_create_destroy", {
        Router *r = router_create();
        if (!r) { printf("FAIL\n"); failed++; return; }
        router_destroy(r);
    });

    T("router_dispatch", {
        Router *r = router_create();
        HttpRequest req; http_request_init(&req);
        HttpResponse res; http_response_init(&res);
        router_add(r, HTTP_GET, "/test", NULL);
        router_dispatch(r, HTTP_GET, "/test", &req, &res);
        http_request_free(&req); http_response_free(&res);
        router_destroy(r);
    });

    printf("\n[JSON]\n");
    T("json_parse_object", {
        JsonValue *v = json_parse("{\"key\":\"value\"}");
        if (!v || json_object_size(v) != 1) { printf("FAIL\n"); failed++; json_value_free(v); return; }
        json_value_free(v);
    });

    T("json_parse_array", {
        JsonValue *v = json_parse("[1,2,3]");
        if (!v || json_array_size(v) != 3) { printf("FAIL\n"); failed++; json_value_free(v); return; }
        json_value_free(v);
    });

    T("json_build_serialize", {
        JsonValue *obj = json_build_object();
        json_object_set(obj, "status", json_build_string("ok"));
        char buf[256];
        int len = json_serialize(obj, buf, sizeof(buf));
        if (len <= 0) { printf("FAIL\n"); failed++; json_value_free(obj); return; }
        json_value_free(obj);
    });

    printf("\n[WebSocket]\n");
    T("ws_sha1", {
        uint8_t d[20];
        ws_sha1((const uint8_t*)"abc", 3, d);
        if (d[0] != 0xa9) { printf("FAIL\n"); failed++; return; }
    });

    T("ws_base64", {
        char out[32];
        ws_base64_encode((const uint8_t*)"hello", 5, out, sizeof(out));
        if (strcmp(out, "aGVsbG8=") != 0) { printf("FAIL\n"); failed++; return; }
    });

    printf("\n[Session]\n");
    T("session_basic", {
        SessionStore store;
        session_store_init(&store);
        Session *s = session_create(&store);
        if (!s) { printf("FAIL\n"); failed++; return; }
    });

    printf("\n[Static Serve]\n");
    T("static_mime", {
        StaticConfig cfg;
        static_config_init(&cfg, ".");
        static_config_load_default_mimes(&cfg);
        const char *m = static_get_mime_type(&cfg, "/test.html");
        if (!strstr(m, "text/html")) { printf("FAIL\n"); failed++; return; }
    });

    printf("\n[CGI]\n");
    T("cgi_config", {
        CgiConfig cfg;
        cgi_config_init(&cfg, "/bin/echo");
    });

    printf("\n========================================\n");
    printf(" Results: %d passed, %d failed\n", passed, failed);
    printf("========================================\n");
    return failed > 0 ? 1 : 0;
}
