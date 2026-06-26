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
    printf("=== Test Step 2 ===\n");
    printf("[config_center]\n");
    cc_config_center_t *c = cc_center_create();
    check(c != NULL, "cc_create");
    check(cc_config_put(c, "app", "db", "host", "localhost") == 0, "cc_put");
    cc_config_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    check(cc_config_get(c, "app", "db", "host", &entry) == 0, "cc_get");
    check(strcmp(entry.value, "localhost") == 0, "cc_get_value");
    check(entry.version == 1, "cc_version");
    cc_center_destroy(c);
    printf("Results: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
