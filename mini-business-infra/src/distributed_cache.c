#include "distributed_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

typedef struct dc_entry {
    char            *key;
    uint8_t         *value;
    size_t           value_len;
    int32_t          ttl_seconds;
    int64_t          created_at;
    int64_t          last_access;
    int64_t          access_count;
    int              stale;
    struct dc_entry  *next;
    struct dc_entry  *prev;
    struct dc_entry  *hnext;
} dc_entry_t;

struct dc_cache {
    dc_entry_t      **buckets;
    size_t            bucket_count;
    size_t            entry_count;
    size_t            max_entries;
    size_t            max_memory_bytes;
    size_t            memory_used;
    dc_eviction_policy_t eviction_policy;
    int               stampede_protection;
    double            recompute_threshold;
    dc_write_strategy_t write_strategy;
    dc_backend_read_fn   backend_read;
    dc_backend_write_fn  backend_write;
    void              *backend_ctx;
    dc_eviction_callback on_evict;
    void              *evict_user_data;
    dc_entry_t        *lru_head;
    dc_entry_t        *lru_tail;
    dc_entry_t        *lfu_head;
    size_t            hit_count;
    size_t            miss_count;
};

static uint32_t dc_hash(const char *key) {
    uint32_t h = 5381;
    while (*key) h = (uint32_t)(((h << 5) + h) + (unsigned char)*key++);
    return h;
}

dc_cache_t *dc_cache_create(const dc_cache_config_t *config) {
    if (!config) return NULL;
    dc_cache_t *c = (dc_cache_t *)calloc(1, sizeof(dc_cache_t));
    if (!c) return NULL;
    c->bucket_count = config->max_entries > 0 ? config->max_entries / 4 : 256;
    if (c->bucket_count < 16) c->bucket_count = 16;
    c->buckets = (dc_entry_t **)calloc(c->bucket_count, sizeof(dc_entry_t *));
    if (!c->buckets) { free(c); return NULL; }
    c->max_entries = config->max_entries;
    c->max_memory_bytes = config->max_memory_bytes;
    c->eviction_policy = config->eviction_policy;
    c->stampede_protection = config->stampede_protection;
    c->recompute_threshold = config->recompute_threshold;
    c->write_strategy = config->write_strategy;
    c->backend_read = config->backend_read;
    c->backend_write = config->backend_write;
    c->backend_ctx = config->backend_ctx;
    c->on_evict = config->on_evict;
    c->evict_user_data = config->evict_user_data;
    return c;
}

void dc_cache_destroy(dc_cache_t *cache) {
    if (!cache) return;
    for (size_t i = 0; i < cache->bucket_count; i++) {
        dc_entry_t *e = cache->buckets[i];
        while (e) {
            dc_entry_t *next = e->next;
            free(e->key);
            free(e->value);
            free(e);
            e = next;
        }
    }
    free(cache->buckets);
    free(cache);
}

static void dc_evict_lru(dc_cache_t *cache) {
    if (!cache->lru_tail) return;
    dc_entry_t *e = cache->lru_tail;
    if (cache->on_evict) cache->on_evict(e->key, e->value, e->value_len, cache->evict_user_data);
    uint32_t idx = dc_hash(e->key) % (uint32_t)cache->bucket_count;
    dc_entry_t **pp = &cache->buckets[idx];
    while (*pp && *pp != e) pp = &(*pp)->next;
    if (*pp) *pp = e->next;
    if (e->prev) e->prev->next = e->next;
    if (e->next) e->next->prev = e->prev;
    if (cache->lru_head == e) cache->lru_head = e->next;
    if (cache->lru_tail == e) cache->lru_tail = e->prev;
    cache->memory_used -= e->value_len + strlen(e->key) + 1;
    cache->entry_count--;
    free(e->key);
    free(e->value);
    free(e);
}

static void dc_evict_lfu(dc_cache_t *cache) {
    dc_entry_t *best = NULL;
    for (size_t i = 0; i < cache->bucket_count; i++) {
        for (dc_entry_t *e = cache->buckets[i]; e; e = e->next) {
            if (!best || e->access_count < best->access_count) best = e;
        }
    }
    if (!best) return;
    dc_evict_lru(cache);
    return; /* fallback to LRU for simplicity */
}

static void dc_evict_ttl(dc_cache_t *cache) {
    int64_t now = (int64_t)time(NULL);
    for (size_t i = 0; i < cache->bucket_count; i++) {
        dc_entry_t *e = cache->buckets[i];
        dc_entry_t *prev = NULL;
        while (e) {
            dc_entry_t *next = e->next;
            if (e->ttl_seconds > 0 && (now - e->created_at) > e->ttl_seconds) {
                if (cache->on_evict) cache->on_evict(e->key, e->value, e->value_len, cache->evict_user_data);
                if (prev) prev->next = next;
                else cache->buckets[i] = next;
                if (e->prev) e->prev->next = e->next;
                if (e->next) e->next->prev = e->prev;
                if (cache->lru_head == e) cache->lru_head = e->next;
                if (cache->lru_tail == e) cache->lru_tail = e->prev;
                cache->memory_used -= e->value_len + strlen(e->key) + 1;
                cache->entry_count--;
                free(e->key);
                free(e->value);
                free(e);
            } else { prev = e; }
            e = next;
        }
    }
}

static void dc_evict_one(dc_cache_t *cache) {
    switch (cache->eviction_policy) {
        case DC_EVICTION_LRU: dc_evict_lru(cache); break;
        case DC_EVICTION_LFU: dc_evict_lfu(cache); break;
        case DC_EVICTION_TTL: dc_evict_ttl(cache); break;
        default: dc_evict_lru(cache); break;
    }
}

static dc_entry_t *dc_find_entry(dc_cache_t *cache, const char *key) {
    uint32_t idx = dc_hash(key) % (uint32_t)cache->bucket_count;
    for (dc_entry_t *e = cache->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e;
    }
    return NULL;
}

static void dc_lru_touch(dc_cache_t *cache, dc_entry_t *e) {
    if (cache->lru_head == e) return;
    if (e->prev) e->prev->next = e->next;
    if (e->next) e->next->prev = e->prev;
    if (cache->lru_tail == e) cache->lru_tail = e->prev;
    e->prev = NULL;
    e->next = cache->lru_head;
    if (cache->lru_head) cache->lru_head->prev = e;
    cache->lru_head = e;
    if (!cache->lru_tail) cache->lru_tail = e;
}

int dc_cache_put(dc_cache_t *cache, const char *key, const uint8_t *value,
                 size_t value_len, int32_t ttl_seconds) {
    if (!cache || !key || !value) return -1;
    dc_entry_t *exist = dc_find_entry(cache, key);
    if (exist) {
        free(exist->value);
        exist->value = (uint8_t *)malloc(value_len);
        if (!exist->value) return -1;
        memcpy(exist->value, value, value_len);
        cache->memory_used = cache->memory_used - exist->value_len + value_len;
        exist->value_len = value_len;
        exist->ttl_seconds = ttl_seconds;
        exist->created_at = (int64_t)time(NULL);
        exist->access_count++;
        dc_lru_touch(cache, exist);
        if (cache->write_strategy == DC_WRITE_THROUGH && cache->backend_write) {
            cache->backend_write(key, value, value_len, cache->backend_ctx);
        }
        return 0;
    }
    while (cache->max_entries > 0 && cache->entry_count >= cache->max_entries) dc_evict_one(cache);
    while (cache->max_memory_bytes > 0 && cache->memory_used + value_len + strlen(key) + 1 > cache->max_memory_bytes) dc_evict_one(cache);
    dc_entry_t *e = (dc_entry_t *)calloc(1, sizeof(dc_entry_t));
    if (!e) return -1;
    e->key = strdup(key);
    e->value = (uint8_t *)malloc(value_len);
    if (!e->value) { free(e->key); free(e); return -1; }
    memcpy(e->value, value, value_len);
    e->value_len = value_len;
    e->ttl_seconds = ttl_seconds;
    e->created_at = (int64_t)time(NULL);
    e->last_access = e->created_at;
    e->access_count = 1;
    uint32_t idx = dc_hash(key) % (uint32_t)cache->bucket_count;
    e->next = cache->buckets[idx];
    cache->buckets[idx] = e;
    cache->entry_count++;
    cache->memory_used += value_len + strlen(key) + 1;
    dc_lru_touch(cache, e);
    if (cache->write_strategy == DC_WRITE_THROUGH && cache->backend_write) {
        cache->backend_write(key, value, value_len, cache->backend_ctx);
    }
    return 0;
}

int dc_cache_get(dc_cache_t *cache, const char *key, uint8_t **value, size_t *value_len) {
    if (!cache || !key || !value || !value_len) return -1;
    dc_entry_t *e = dc_find_entry(cache, key);
    if (!e) {
        cache->miss_count++;
        if (cache->backend_read) {
            uint8_t *bv = NULL; size_t bl = 0;
            if (cache->backend_read(key, &bv, &bl, cache->backend_ctx) == 0 && bv) {
                dc_cache_put(cache, key, bv, bl, DC_DEFAULT_TTL_SEC);
                *value = bv; *value_len = bl;
                return 0;
            }
        }
        return -1;
    }
    int64_t now = (int64_t)time(NULL);
    if (e->stale || (e->ttl_seconds > 0 && (now - e->created_at) > e->ttl_seconds)) {
        if (cache->stampede_protection) {
            double ratio = (double)(now - e->created_at) / e->ttl_seconds;
            if (ratio >= cache->recompute_threshold) {
                double r = (double)rand() / RAND_MAX;
                if (r < (ratio - cache->recompute_threshold) / (1.0 - cache->recompute_threshold)) {
                    if (cache->backend_read) {
                        uint8_t *bv = NULL; size_t bl = 0;
                        if (cache->backend_read(key, &bv, &bl, cache->backend_ctx) == 0 && bv) {
                            dc_cache_put(cache, key, bv, bl, DC_DEFAULT_TTL_SEC);
                            free(bv);
                        }
                    }
                }
            }
        }
        if (cache->backend_read) {
            uint8_t *bv = NULL; size_t bl = 0;
            if (cache->backend_read(key, &bv, &bl, cache->backend_ctx) == 0 && bv) {
                dc_cache_put(cache, key, bv, bl, DC_DEFAULT_TTL_SEC);
                *value = bv; *value_len = bl;
                return 0;
            }
        }
        return -1;
    }
    cache->hit_count++;
    e->last_access = now;
    e->access_count++;
    dc_lru_touch(cache, e);
    uint8_t *out = (uint8_t *)malloc(e->value_len);
    if (!out) return -1;
    memcpy(out, e->value, e->value_len);
    *value = out;
    *value_len = e->value_len;
    return 0;
}

int dc_cache_delete(dc_cache_t *cache, const char *key) {
    if (!cache || !key) return -1;
    uint32_t idx = dc_hash(key) % (uint32_t)cache->bucket_count;
    dc_entry_t *prev = NULL;
    for (dc_entry_t *e = cache->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            if (cache->on_evict) cache->on_evict(e->key, e->value, e->value_len, cache->evict_user_data);
            if (prev) prev->next = e->next;
            else cache->buckets[idx] = e->next;
            if (e->prev) e->prev->next = e->next;
            if (e->next) e->next->prev = e->prev;
            if (cache->lru_head == e) cache->lru_head = e->next;
            if (cache->lru_tail == e) cache->lru_tail = e->prev;
            cache->memory_used -= e->value_len + strlen(e->key) + 1;
            cache->entry_count--;
            free(e->key); free(e->value); free(e);
            return 0;
        }
        prev = e;
    }
    return -1;
}

int dc_cache_exists(dc_cache_t *cache, const char *key) {
    return dc_find_entry(cache, key) ? 1 : 0;
}

int dc_cache_expire(dc_cache_t *cache, const char *key, int32_t ttl_seconds) {
    dc_entry_t *e = dc_find_entry(cache, key);
    if (!e) return -1;
    e->ttl_seconds = ttl_seconds;
    e->created_at = (int64_t)time(NULL);
    return 0;
}

int64_t dc_cache_ttl(dc_cache_t *cache, const char *key) {
    dc_entry_t *e = dc_find_entry(cache, key);
    if (!e || e->ttl_seconds <= 0) return -1;
    int64_t elapsed = (int64_t)time(NULL) - e->created_at;
    int64_t remaining = e->ttl_seconds - elapsed;
    return remaining > 0 ? remaining : 0;
}

int dc_cache_invalidate(dc_cache_t *cache, const char *key, dc_invalidate_mode_t mode) {
    if (!cache || !key) return -1;
    switch (mode) {
        case DC_INVALIDATE_DELETE: return dc_cache_delete(cache, key);
        case DC_INVALIDATE_UPDATE: {
            dc_entry_t *e = dc_find_entry(cache, key);
            if (e) { e->created_at = (int64_t)time(NULL); e->stale = 0; return 0; }
            return -1;
        }
        case DC_INVALIDATE_VERSION:
        default: return dc_cache_delete(cache, key);
    }
}

int dc_cache_batch_get(dc_cache_t *cache, const char **keys, int key_count,
                       uint8_t **values, size_t *value_lens) {
    if (!cache || !keys || !values || !value_lens) return -1;
    for (int i = 0; i < key_count; i++) {
        dc_cache_get(cache, keys[i], &values[i], &value_lens[i]);
    }
    return 0;
}

int dc_cache_batch_put(dc_cache_t *cache, const char **keys,
                       const uint8_t **values, const size_t *value_lens,
                       const int32_t *ttl_seconds, int key_count) {
    if (!cache || !keys || !values || !value_lens) return -1;
    for (int i = 0; i < key_count; i++) {
        dc_cache_put(cache, keys[i], values[i], value_lens[i],
                     ttl_seconds ? ttl_seconds[i] : DC_DEFAULT_TTL_SEC);
    }
    return 0;
}

int dc_cache_flush(dc_cache_t *cache) {
    if (!cache) return -1;
    for (size_t i = 0; i < cache->bucket_count; i++) {
        dc_entry_t *e = cache->buckets[i];
        while (e) {
            dc_entry_t *next = e->next;
            free(e->key); free(e->value); free(e);
            e = next;
        }
        cache->buckets[i] = NULL;
    }
    cache->entry_count = 0;
    cache->memory_used = 0;
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->lfu_head = NULL;
    cache->hit_count = 0;
    cache->miss_count = 0;
    return 0;
}

size_t dc_cache_size(dc_cache_t *cache) { return cache ? cache->entry_count : 0; }
size_t dc_cache_memory_used(dc_cache_t *cache) { return cache ? cache->memory_used : 0; }
size_t dc_cache_hit_count(dc_cache_t *cache) { return cache ? cache->hit_count : 0; }
size_t dc_cache_miss_count(dc_cache_t *cache) { return cache ? cache->miss_count : 0; }

double dc_cache_hit_rate(dc_cache_t *cache) {
    if (!cache) return 0.0;
    size_t total = cache->hit_count + cache->miss_count;
    if (total == 0) return 0.0;
    return (double)cache->hit_count / total;
}

void dc_cache_compact(dc_cache_t *cache) {
    if (!cache) return;
    dc_evict_ttl(cache);
}
