#include "middleware.h"
#include <stdio.h>
#include <string.h>

static MiddlewareResult custom_timer(HttpRequest *req, HttpResponse *res,
                                      MiddlewareContext *ctx) {
    (void)req;
    (void)ctx;
    http_response_add_header(res, "X-Response-Time", "2ms");
    printf("  [timer] added X-Response-Time header\n");
    return MIDDLEWARE_NEXT;
}

static MiddlewareResult custom_auth_check(HttpRequest *req, HttpResponse *res,
                                           MiddlewareContext *ctx) {
    (void)ctx;
    const char *token = http_request_get_header(req, "X-Api-Key");
    if (!token || strcmp(token, "secret123") != 0) {
        printf("  [auth] INVALID key, blocking request\n");
        http_response_set_status(res, HTTP_STATUS_FORBIDDEN);
        http_response_set_body_str(res, "{\"error\":\"invalid api key\"}");
        return MIDDLEWARE_STOP;
    }
    printf("  [auth] valid key, passing through\n");
    return MIDDLEWARE_NEXT;
}

int main(void) {
    printf("=== Middleware Chain Demo ===\n\n");

    /* Build chain */
    MiddlewareChain chain;
    middleware_chain_init(&chain);
    middleware_chain_add(&chain, middleware_logger,      NULL);
    middleware_chain_add(&chain, middleware_cors,        NULL);
    middleware_chain_add(&chain, custom_timer,           NULL);
    middleware_chain_add(&chain, custom_auth_check,      NULL);
    middleware_chain_add(&chain, middleware_compress_gzip, NULL);

    printf("Chain has %d middleware entries\n\n", chain.count);

    /* Scenario A: Valid request with API key */
    printf("[SCENARIO A] Request with valid X-Api-Key\n");
    HttpRequest req_a;
    http_request_init(&req_a);
    http_parse_request_line("GET /api/data HTTP/1.1", &req_a);
    http_parse_header("X-Api-Key: secret123", &req_a);
    http_parse_header("Accept-Encoding: gzip", &req_a);

    HttpResponse res_a;
    http_response_init(&res_a);
    http_response_set_body_str(&res_a, "{\"data\":[1,2,3]}");

    MiddlewareContext ctx_a = {0};
    bool ok = middleware_chain_execute(&chain, &req_a, &res_a, &ctx_a);
    printf("  Result: %s (status=%d)\n\n", ok ? "pass" : "blocked",
           res_a.status_code);

    http_request_free(&req_a);
    http_response_free(&res_a);

    /* Scenario B: Missing API key */
    printf("[SCENARIO B] Request WITHOUT X-Api-Key\n");
    HttpRequest req_b;
    http_request_init(&req_b);
    http_parse_request_line("GET /api/data HTTP/1.1", &req_b);

    HttpResponse res_b;
    http_response_init(&res_b);
    http_response_set_body_str(&res_b, "{\"data\":[1,2,3]}");

    MiddlewareContext ctx_b = {0};
    ok = middleware_chain_execute(&chain, &req_b, &res_b, &ctx_b);
    printf("  Result: %s (status=%d)\n",
           ok ? "pass" : "blocked", res_b.status_code);

    /* Print response body from scenario B */
    if (res_b.body) {
        printf("  Body: %.*s\n\n", (int)res_b.body_len, res_b.body);
    }

    http_request_free(&req_b);
    http_response_free(&res_b);

    /* Scenario C: CORS preflight */
    printf("[SCENARIO C] CORS preflight OPTIONS request\n");
    HttpRequest req_c;
    http_request_init(&req_c);
    http_parse_request_line("OPTIONS /api/data HTTP/1.1", &req_c);
    http_parse_header("Origin: https://example.com", &req_c);
    http_parse_header("Access-Control-Request-Method: POST", &req_c);

    HttpResponse res_c;
    http_response_init(&res_c);

    MiddlewareContext ctx_c = {0};
    ok = middleware_chain_execute(&chain, &req_c, &res_c, &ctx_c);

    printf("  Result: %s (status=%d)\n", ok ? "pass" : "stopped",
           res_c.status_code);
    for (int i = 0; i < res_c.header_count; i++) {
        printf("  %s: %s\n", res_c.headers[i].name, res_c.headers[i].value);
    }

    http_request_free(&req_c);
    http_response_free(&res_c);

    /* CORS custom config */
    printf("\n[CORS CUSTOM CONFIG]\n");
    middleware_cors_set_origin("https://myapp.local");
    middleware_cors_set_methods("GET, POST");
    printf("  Origin set to: https://myapp.local\n");
    printf("  Methods set to: GET, POST\n");

    middleware_chain_clear(&chain);
    printf("\n=== Done ===\n");
    return 0;
}
