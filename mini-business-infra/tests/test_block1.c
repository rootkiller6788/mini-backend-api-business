#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config_center.h"
#include "distributed_cache.h"
#include "gateway_routing.h"
#include "message_queue.h"
#include "service_registry.h"
#include "circuit_breaker.h"
#include "consistent_hash.h"
#include "bloom_filter.h"

static int tests_run = 0;
static int tests_pass = 0;
static void check(int cond, const char *name) {
    tests_run++;
    if (cond) { tests_pass++; printf("  PASS: %s\n", name); }
    else { printf("  FAIL: %s\n", name); }
}

int main(void) {
    srand((unsigned)time(NULL));
    printf("=== Test ===\n");
    int cnt;
    printf("[config_center]\n");
    cc_config_center_t *c = cc_center_create();
    check(c != NULL, "cc_create");
    check(cc_config_put(c, "app", "db", "host", "localhost") == 0, "cc_put");
    cc_config_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    check(cc_config_get(c, "app", "db", "host", &entry) == 0, "cc_get");
    cc_center_destroy(c);
    printf("Results: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
