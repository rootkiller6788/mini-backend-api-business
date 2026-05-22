#include "distributed_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int mock_db_read(const char *key, uint8_t **value, size_t *len, void *ctx) {
    (void)ctx;
    printf("[backend] Cache miss for '%s', loading from DB...\n", key);
    const char *fake = "mock_db_value";
    *len = strlen(fake);
    *value = (uint8_t *)malloc(*len);
    memcpy(*value, fake, *len);
    return 0;
}

static int mock_db_write(const char *key, const uint8_t *value, size_t len, void *ctx) {
    (void)ctx;
    printf("[backend] Write-through: '%s' = '%.*s'\n", key, (int)len, (const char *)value);
    return 0;
}

static void on_evict(const char *key, const uint8_t *value, size_t len, void *user_data) {
    (void)user_data;
    printf("[evict] key='%s' value_len=%zu\n", key, len);
}

int main(void) {
    dc_cache_config_t config = {0};
    config.max_entries = 100;
    config.max_memory_bytes = 10 * 1024 * 1024;
    config.eviction_policy = DC_EVICTION_LRU;
    config.stampede_protection = 1;
    config.recompute_threshold = 0.8;
    config.write_strategy = DC_WRITE_THROUGH;
    config.backend_read = mock_db_read;
    config.backend_write = mock_db_write;
    config.on_evict = on_evict;

    dc_cache_t *cache = dc_cache_create(&config);
    if (!cache) { fprintf(stderr, "Failed to create cache\n"); return 1; }

    dc_cache_put(cache, "user:1001", (const uint8_t *)"Alice", 5, 300);
    dc_cache_put(cache, "user:1002", (const uint8_t *)"Bob", 3, 300);
    dc_cache_put(cache, "user:1003", (const uint8_t *)"Charlie", 7, 10);

    uint8_t *val = NULL; size_t vlen = 0;
    if (dc_cache_get(cache, "user:1001", &val, &vlen) == 0) {
        printf("[get] user:1001 = '%.*s' (len=%zu)\n", (int)vlen, (const char *)val, vlen);
        free(val);
    }
    if (dc_cache_get(cache, "user:1003", &val, &vlen) == 0) {
        printf("[get] user:1003 = '%.*s' (len=%zu)\n", (int)vlen, (const char *)val, vlen);
        free(val);
    }

    int64_t ttl = dc_cache_ttl(cache, "user:1003");
    printf("[ttl] user:1003 remaining=%llds\n", (long long)ttl);

    printf("[cache] size=%zu memory=%zu hit_rate=%.2f%%\n",
           dc_cache_size(cache), dc_cache_memory_used(cache),
           dc_cache_hit_rate(cache) * 100.0);

    printf("[exists] user:1001=%d user:9999=%d\n",
           dc_cache_exists(cache, "user:1001"),
           dc_cache_exists(cache, "user:9999"));

    dc_cache_invalidate(cache, "user:1002", DC_INVALIDATE_DELETE);

    dc_cache_delete(cache, "user:1001");
    dc_cache_flush(cache);

    dc_cache_destroy(cache);
    printf("Distributed cache example completed.\n");
    return 0;
}
