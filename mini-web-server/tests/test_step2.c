#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_core.h"
#include "router.h"
static int g_pass = 0, g_fail = 0;
#define T(name) printf("  TEST %-45s ", name)
#define OK() do { printf("PASS\n"); g_pass++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); g_fail++; } while(0)
#define CHK(cond, name) do { T(name); if (cond) OK(); else FAIL(#cond); } while(0)
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\n========== Test ==========\n\n");
    printf("[HTTP Core]\n");
    CHK(strcmp(http_method_str(HTTP_GET), "GET") == 0, "method_str");
    printf("\n[Router]\n");
    { Router *r = router_create(); CHK(r != NULL, "create"); router_destroy(r); }
    { HttpRequest *req = calloc(1, sizeof(HttpRequest)); http_request_init(req);
      http_parse_request_line("GET / HTTP/1.1", req);
      HttpResponse *res = calloc(1, sizeof(HttpResponse)); http_response_init(res);
      Router *r = router_create(); router_add(r, HTTP_GET, "/test", NULL);
      CHK(router_dispatch(r, HTTP_GET, "/test", req, res), "dispatch");
      http_request_free(req); free(req); http_response_free(res); free(res);
      router_destroy(r); }
    printf("\nResults: %d/%d passed\n", g_pass, g_pass+g_fail);
    return g_fail > 0 ? 1 : 0;
}
