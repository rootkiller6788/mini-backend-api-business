#include <stdio.h>
#include <string.h>
#include "config_center.h"

static int tests_run = 0;
static int tests_pass = 0;
static void check(int cond, const char *name) {
    tests_run++;
    if (cond) { tests_pass++; printf("  PASS: %s\n", name); }
    else { printf("  FAIL: %s\n", name); }
}

int main(void) {
    printf("=== Test Step 1 ===\n");
    printf("[config_center]\n");
    cc_config_center_t *c = cc_center_create();
    check(c != NULL, "cc_create");
    cc_config_put(c, "app", "db", "host", "localhost");
    cc_center_destroy(c);
    printf("Results: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
