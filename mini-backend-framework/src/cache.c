/*
 * cache.c �� LRU Cache Implementation
 *
 * L4: LRU (Least Recently Used) is k-competitive where k = cache size.
 * Formal result (Sleator & Tarjan, 1985): No deterministic online
 * paging algorithm has competitive ratio better than k. LRU achieves
 * exactly k, making it optimal among deterministic online algorithms.
 *
 * L5: Doubly-linked list + hash table hybrid data structure:
 *   - Hash table: maps key -> CacheEntry* for O(1) average lookup
 *   - Doubly-linked list: maintains access order (head = MRU, tail = LRU)
 *   - On access: detach node from current position, move to head
 *   - On eviction: remove tail node
 *   All operations O(1) average time.
 *
 * L8: TTL (Time-To-Live) lazy expiration �� expired entries are
 * removed on access (get/put). Active cleanup via cache_evict_expired().
 *
 * Reference: Sleator & Tarjan, "Amortized Efficiency of List Update
 * and Paging Rules" (CACM 1985); Hennessy & Patterson sec 2.3.
 */

#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* djb2 hash function (Dan Bernstein) �� simple, fast, good distribution */
static unsigned long hash_djb2(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

/* Check if entry is expired based on TTL */
static bool entry_expired(const CacheEntry *entry) {
    struct timespec now;
    double age;
    if (!entry || entry->ttl_sec <= 0) return false;
    clock_gettime(CLOCK_MONOTONIC, &now);
    age = (double)(now.tv_sec - entry->created_at.tv_sec);
    age += (double)(now.tv_nsec - entry->created_at.tv_nsec) / 1e9;
    return age >= (double)entry->ttl_sec;
}

/* Detach node from doubly-linked list (but do not free it) */
static void detach_node(LRUCache *cache, CacheEntry *entry) {
    if (entry->prev) entry->prev->next = entry->next;
    else cache->head = entry->next;
    if (entry->next) entry->next->prev = entry->prev;
    else cache->tail = entry->prev;
    entry->prev = entry->next = NULL;
}

/* Move node to head (MRU position) */
static void move_to_head(LRUCache *cache, CacheEntry *entry) {
    if (cache->head == entry) return;
    detach_node(cache, entry);
    entry->next = cache->head;
    entry->prev = NULL;
    if (cache->head) cache->head->prev = entry;
    cache->head = entry;
    if (!cache->tail) cache->tail = entry;
}

static void hash_remove(LRUCache *cache, CacheEntry *entry) {
    unsigned long h = hash_djb2(entry->key) % CACHE_HASH_SIZE;
    CacheEntry **pp = &cache->hash[h];
    while (*pp) {
        if (*pp == entry) { *pp = entry->hash_next; return; }
        pp = &(*pp)->hash_next;
    }
}

static void hash_insert(LRUCache *cache, CacheEntry *entry) {
    unsigned long h = hash_djb2(entry->key) % CACHE_HASH_SIZE;
    entry->hash_next = cache->hash[h];
    cache->hash[h] = entry;
}

static CacheEntry *hash_find(LRUCache *cache, const char *key) {
    unsigned long h = hash_djb2(key) % CACHE_HASH_SIZE;
    CacheEntry *entry = cache->hash[h];
    while (entry) {
        if (strcmp(entry->key, key) == 0) return entry;
        entry = entry->hash_next;
    }
    return NULL;
}

static void free_entry(CacheEntry *entry) {
    if (entry) free(entry);
}

void cache_init(LRUCache *cache, int capacity, int default_ttl) {
    if (!cache) return;
    memset(cache, 0, sizeof(LRUCache));
    cache->capacity = capacity > 0 ? capacity : 64;
    cache->default_ttl = default_ttl >= 0 ? default_ttl : 0;
}

const char *cache_get(LRUCache *cache, const char *key) {
    CacheEntry *entry;
    if (!cache || !key) return NULL;
    entry = hash_find(cache, key);
    if (!entry) { cache->misses++; return NULL; }
    if (entry_expired(entry)) {
        detach_node(cache, entry);
        hash_remove(cache, entry);
        free_entry(entry);
        cache->size--;
        cache->evictions++;
        cache->misses++;
        return NULL;
    }
    move_to_head(cache, entry);
    cache->hits++;
    return entry->value;
}

int cache_put(LRUCache *cache, const char *key, const char *value, int ttl_sec) {
    CacheEntry *entry;
    int val_len;
    if (!cache || !key || !value) return -1;
    entry = hash_find(cache, key);
    if (entry) {
        val_len = (int)strlen(value);
        if (val_len >= CACHE_MAX_VALUE_LEN) val_len = CACHE_MAX_VALUE_LEN - 1;
        memcpy(entry->value, value, val_len);
        entry->value[val_len] = 0;
        entry->value_len = val_len;
        entry->ttl_sec = ttl_sec > 0 ? ttl_sec : cache->default_ttl;
        clock_gettime(CLOCK_MONOTONIC, &entry->created_at);
        move_to_head(cache, entry);
        return 0;
    }
    if (cache->size >= cache->capacity) {
        cache_evict_lru(cache);
    }
    entry = (CacheEntry *)calloc(1, sizeof(CacheEntry));
    if (!entry) return -1;
    strncpy(entry->key, key, CACHE_MAX_KEY_LEN - 1);
    entry->key[CACHE_MAX_KEY_LEN - 1] = 0;
    val_len = (int)strlen(value);
    if (val_len >= CACHE_MAX_VALUE_LEN) val_len = CACHE_MAX_VALUE_LEN - 1;
    memcpy(entry->value, value, val_len);
    entry->value[val_len] = 0;
    entry->value_len = val_len;
    entry->ttl_sec = ttl_sec > 0 ? ttl_sec : cache->default_ttl;
    clock_gettime(CLOCK_MONOTONIC, &entry->created_at);
    entry->next = cache->head;
    if (cache->head) cache->head->prev = entry;
    cache->head = entry;
    if (!cache->tail) cache->tail = entry;
    hash_insert(cache, entry);
    cache->size++;
    return 0;
}

int cache_delete(LRUCache *cache, const char *key) {
    CacheEntry *entry;
    if (!cache || !key) return -1;
    entry = hash_find(cache, key);
    if (!entry) return -1;
    detach_node(cache, entry);
    hash_remove(cache, entry);
    free_entry(entry);
    cache->size--;
    return 0;
}

bool cache_has(const LRUCache *cache, const char *key) {
    CacheEntry *entry;
    if (!cache || !key) return false;
    entry = hash_find((LRUCache *)cache, key);
    if (!entry) return false;
    if (entry_expired(entry)) return false;
    return true;
}

int cache_evict_expired(LRUCache *cache) {
    CacheEntry *curr, *next;
    int evicted = 0;
    if (!cache) return 0;
    curr = cache->tail;
    while (curr) {
        next = curr->prev;
        if (entry_expired(curr)) {
            hash_remove(cache, curr);
            detach_node(cache, curr);
            free_entry(curr);
            cache->size--;
            cache->evictions++;
            evicted++;
        }
        curr = next;
    }
    return evicted;
}

int cache_evict_lru(LRUCache *cache) {
    CacheEntry *evict;
    if (!cache || !cache->tail) return -1;
    evict = cache->tail;
    detach_node(cache, evict);
    hash_remove(cache, evict);
    free_entry(evict);
    cache->size--;
    cache->evictions++;
    return 0;
}

int cache_size(const LRUCache *cache) {
    return cache ? cache->size : 0;
}

void cache_clear(LRUCache *cache) {
    CacheEntry *curr, *next;
    if (!cache) return;
    for (curr = cache->head; curr; curr = next) {
        next = curr->next;
        free(curr);
    }
    cache->head = cache->tail = NULL;
    cache->size = 0;
    memset(cache->hash, 0, sizeof(cache->hash));
}

void cache_destroy(LRUCache *cache) {
    cache_clear(cache);
    if (cache) cache->capacity = 0;
}

double cache_hit_rate(const LRUCache *cache) {
    int64_t total;
    if (!cache) return 0.0;
    total = cache->hits + cache->misses;
    if (total == 0) return 0.0;
    return (double)cache->hits / (double)total;
}

int64_t cache_hits(const LRUCache *cache) { return cache ? cache->hits : 0; }
int64_t cache_misses(const LRUCache *cache) { return cache ? cache->misses : 0; }
int64_t cache_evictions(const LRUCache *cache) { return cache ? cache->evictions : 0; }
