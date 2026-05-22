#include "config_center.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_config_changed(const char *namespace_, const char *key,
                              const char *new_value, size_t new_len,
                              void *user_data) {
    (void)user_data;
    printf("[hot-reload] ns=%s key=%s value=%.*s\n", namespace_, key, (int)new_len, new_value);
}

int main(void) {
    cc_config_center_t *center = cc_center_create();
    if (!center) { fprintf(stderr, "Failed to create config center\n"); return 1; }

    cc_config_put(center, "production", "database", "mysql.host", "10.0.0.1");
    cc_config_put(center, "production", "database", "mysql.port", "3306");
    cc_config_put(center, "production", "redis", "redis.host", "10.0.0.2");
    cc_config_put(center, "staging", "database", "mysql.host", "10.0.1.1");

    cc_config_entry_t entry;
    if (cc_config_get(center, "production", "database", "mysql.host", &entry) == 0) {
        printf("[get] %s:%s:%s = %s (v%lld)\n",
               entry.namespace_, entry.group_, entry.config_key,
               entry.value, (long long)entry.version);
    }

    cc_config_put(center, "production", "database", "mysql.host", "10.0.0.5");
    if (cc_config_get(center, "production", "database", "mysql.host", &entry) == 0) {
        printf("[updated] %s = %s (v%lld)\n", entry.config_key, entry.value, (long long)entry.version);
    }

    if (cc_config_get_version(center, "production", "database", "mysql.host", 1, &entry) == 0) {
        printf("[history] v1 = %s\n", entry.value);
    }

    cc_config_version_t versions[10]; int vcount = 0;
    cc_config_list_versions(center, "production", "database", "mysql.host", versions, &vcount);
    printf("[versions] count=%d\n", vcount);
    for (int i = 0; i < vcount; i++) {
        printf("  v%lld: %s\n", (long long)versions[i].version, versions[i].value);
    }

    cc_config_rollback(center, "production", "database", "mysql.host", 1);
    if (cc_config_get(center, "production", "database", "mysql.host", &entry) == 0) {
        printf("[rollback] %s = %s (v%lld)\n", entry.config_key, entry.value, (long long)entry.version);
    }

    cc_config_put_encrypted(center, "production", "secrets", "api.key", "secret-abc-123", "my-secret-key");
    char decrypted[128];
    if (cc_config_get_decrypted(center, "production", "secrets", "api.key",
                                decrypted, sizeof(decrypted), "my-secret-key") == 0) {
        printf("[encrypted] api.key = %s\n", decrypted);
    }

    cc_subscribe(center, "production", on_config_changed, NULL);
    cc_config_put(center, "production", "app", "feature.x", "enabled");

    cc_gray_release_t gray = {0};
    gray.type = CC_GRAY_IP;
    strcpy(gray.target_ips[0], "10.0.5.1");
    gray.ip_count = 1;
    gray.gray_value = strdup("10.0.0.99");
    gray.gray_value_len = strlen(gray.gray_value);
    gray.gray_version = 100;
    cc_gray_release_set(center, "production", "database", "mysql.host", &gray);
    free(gray.gray_value);

    cc_config_entry_t gray_entry;
    if (cc_gray_release_get(center, "production", "database", "mysql.host",
                            "10.0.5.1", &gray_entry) == 0) {
        printf("[gray] instance 10.0.5.1 sees: %s\n", gray_entry.value);
    }
    if (cc_gray_release_get(center, "production", "database", "mysql.host",
                            "10.0.5.2", &gray_entry) == 0) {
        printf("[gray] instance 10.0.5.2 sees: %s\n", gray_entry.value);
    }

    char nss[10][CC_MAX_NAMESPACE_LEN]; int ns_count = 0;
    cc_namespace_list(center, nss, &ns_count);
    printf("[namespaces] count=%d\n", ns_count);
    for (int i = 0; i < ns_count; i++) printf("  %s\n", nss[i]);

    cc_config_delete(center, "staging", "database", "mysql.host");
    cc_center_destroy(center);
    printf("Config center example completed.\n");
    return 0;
}
