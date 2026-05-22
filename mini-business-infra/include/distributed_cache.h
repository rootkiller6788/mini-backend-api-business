#ifndef DISTRIBUTED_CACHE_H
#define DISTRIBUTED_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DC_MAX_KEY_LEN     128
#define DC_MAX_VALUE_LEN   4194304
#define DC_DEFAULT_TTL_SEC 300

typedef enum {
    DC_EVICTION_NONE   = 0,
    DC_EVICTION_LRU    = 1,
    DC_EVICTION_LFU    = 2,
    DC_EVICTION_TTL    = 3
} dc_eviction_policy_t;

typedef enum {
    DC_WRITE_THROUGH = 0,
    DC_WRITE_BEHIND  = 1,
    DC_WRITE_AROUND  = 2
} dc_write_strategy_t;

typedef enum {
    DC_INVALIDATE_DELETE         = 0,
    DC_INVALIDATE_UPDATE         = 1,
    DC_INVALIDATE_VERSION        = 2
} dc_invalidate_mode_t;

typedef struct {
    uint32_t hash_slot;
    int32_t  shard_id;
} dc_cluster_info_t;

typedef int (*dc_backend_read_fn)(const char *key, uint8_t **value, size_t *len, void *ctx);
typedef int (*dc_backend_write_fn)(const char *key, const uint8_t *value, size_t len, void *ctx);
typedef void (*dc_eviction_callback)(const char *key, const uint8_t *value, size_t len, void *user_data);

typedef struct dc_cache dc_cache_t;

typedef struct {
    size_t                 max_entries;
    size_t                 max_memory_bytes;
    dc_eviction_policy_t   eviction_policy;
    int                    stampede_protection;
    double                 recompute_threshold;
    dc_write_strategy_t    write_strategy;
    dc_backend_read_fn     backend_read;
    dc_backend_write_fn    backend_write;
    void                  *backend_ctx;
    dc_eviction_callback   on_evict;
    void                  *evict_user_data;
} dc_cache_config_t;

dc_cache_t  *dc_cache_create(const dc_cache_config_t *config);
void         dc_cache_destroy(dc_cache_t *cache);

int          dc_cache_put(dc_cache_t *cache, const char *key, const uint8_t *value,
                          size_t value_len, int32_t ttl_seconds);
int          dc_cache_get(dc_cache_t *cache, const char *key, uint8_t **value,
                          size_t *value_len);
int          dc_cache_delete(dc_cache_t *cache, const char *key);
int          dc_cache_exists(dc_cache_t *cache, const char *key);
int          dc_cache_expire(dc_cache_t *cache, const char *key, int32_t ttl_seconds);
int64_t      dc_cache_ttl(dc_cache_t *cache, const char *key);

int          dc_cache_invalidate(dc_cache_t *cache, const char *key,
                                 dc_invalidate_mode_t mode);

int          dc_cache_batch_get(dc_cache_t *cache, const char **keys, int key_count,
                                uint8_t **values, size_t *value_lens);
int          dc_cache_batch_put(dc_cache_t *cache, const char **keys,
                                const uint8_t **values, const size_t *value_lens,
                                const int32_t *ttl_seconds, int key_count);

int          dc_cache_flush(dc_cache_t *cache);
size_t       dc_cache_size(dc_cache_t *cache);
size_t       dc_cache_memory_used(dc_cache_t *cache);
size_t       dc_cache_hit_count(dc_cache_t *cache);
size_t       dc_cache_miss_count(dc_cache_t *cache);
double       dc_cache_hit_rate(dc_cache_t *cache);

void         dc_cache_compact(dc_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif
