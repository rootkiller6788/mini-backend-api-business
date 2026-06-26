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
    check(strcmp(entry.value, "localhost") == 0, "cc_get_value");
    check(entry.version == 1, "cc_version");
    check(cc_config_put(c, "app", "db", "host", "v2") == 0, "cc_put_v2");
    memset(&entry, 0, sizeof(entry));
    check(cc_config_get_version(c, "app", "db", "host", 1, &entry) == 0, "cc_get_ver");
    check(strcmp(entry.value, "v1") == 0, "cc_ver_val");
    check(cc_config_put_encrypted(c, "sec", "db", "pass", "secret", "key") == 0, "cc_enc_put");
    char buf[256];
    check(cc_config_get_decrypted(c, "sec", "db", "pass", buf, 256, "key") == 0, "cc_enc_get");
    check(strcmp(buf, "secret") == 0, "cc_enc_val");
    char nss[10][64];
    check(cc_namespace_list(c, nss, &cnt) == 0, "cc_ns_list");
    char grps[10][64];
    check(cc_group_list(c, "app", grps, &cnt) == 0, "cc_grp_list");
    char keys[10][128];
    check(cc_key_list(c, "app", "db", keys, &cnt) == 0, "cc_key_list");
    cc_config_put(c, "ns_a", "g1", "k1", "v1");
    cc_config_put(c, "ns_a", "g1", "k2", "v2");
    cc_config_put(c, "ns_b", "g1", "k1", "v1");
    char diff[10][128];
    check(cc_config_diff(c, "ns_a", "g1", "ns_b", "g1", diff, &cnt) == 0, "cc_diff");
    check(cnt == 1, "cc_diff_cnt");
    cc_center_destroy(c);

    printf("Results: %d/%d\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
