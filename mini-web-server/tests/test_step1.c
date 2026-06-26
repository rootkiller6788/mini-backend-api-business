#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_core.h"
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
    CHK(http_method_from_str("POST") == HTTP_POST, "from_str");
    printf("\nResults: %d/%d passed\n", g_pass, g_pass+g_fail);
    return g_fail > 0 ? 1 : 0;
}
