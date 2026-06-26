#include <stdio.h>
#include <string.h>
#include "config_center.h"
#include "distributed_cache.h"

static int tests_run = 0;
static int tests_pass = 0;
static void check(int cond, const char *name) {
    tests_run++;
    if (cond) { tests_pass++; printf("  PASS: %s\n", name); }
    else { printf("  FAIL: %s\n", name); }
}

int main(void) {
    printf("=== Step 4 ===\n");
    int cnt;

    printf("[config_center]\n");
    cc_config_center_t *c = cc_center_create();
    check(c != NULL, "cc_create");
    check(cc_config_put(c, "app", "db", "host", "localhost") == 0, "cc_put");
    cc_config_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    check(cc_config_get(c, "app", "db", "host", &entry) == 0, "cc_get");
    check(strcmp(entry.value, "localhost") == 0, "cc_get_value");
    cc_center_destroy(c);

    printf("[distributed_cache]\n");
    dc_cache_config_t dcfg = { .max_entries = 50 };
    dc_cache_t *dc = dc_cache_create(&dcfg);
    check(dc != NULL, "dc_create");
    uint8_t v1[] = "hello";
    check(dc_cache_put(dc, "k1", v1, 5, 60) == 0, "dc_put");
    uint8_t *vout = NULL; size_t vlen = 0;
    check(dc_cache_get(dc, "k1", &vout, &vlen) == 0, "dc_get");
    check(vlen == 5 && memcmp(vout, "hello", 5) == 0, "dc_get_val");
    free(vout);
    dc_cache_destroy(dc);

    printf("Results: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
