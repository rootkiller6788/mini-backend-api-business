#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

/*
 * L1: Core Definitions — In-Memory LRU Cache
 *
 * A key-value cache with Least Recently Used (LRU) eviction policy.
 * When the cache exceeds its capacity, the least recently accessed
 * entry is evicted to make room for new entries.
 *
 * L4: LRU optimality — under the "locality of reference" assumption,
 * LRU minimizes expected cache misses among online policies.
 * Formal proof: Belady's MIN is optimal offline; LRU is k-competitive
 * (Sleator & Tarjan, 1985, "Amortized Efficiency of List Update").
 *
 * L5: Implementation uses two data structures:
 *   1. Doubly-linked list for O(1) move-to-front on access
 *   2. Hash table (chaining) for O(1) average lookup
 * Combined complexity: O(1) average for get/put/delete.
 *
 * L8: TTL (Time-To-Live) support — entries expire after a configurable
 * duration. Expired entries are lazily evicted on access.
 */

#define CACHE_MAX_KEY_LEN   128
#define CACHE_MAX_VALUE_LEN 1024
#define CACHE_HASH_SIZE     256

/* Cache entry node in doubly-linked list */
typedef struct CacheEntrySt {
    char    key[CACHE_MAX_KEY_LEN];
    char    value[CACHE_MAX_VALUE_LEN];
    int     value_len;
    struct timespec created_at;
    int     ttl_sec;               /* 0 = no expiration */
    struct CacheEntrySt *prev;
    struct CacheEntrySt *next;
    struct CacheEntrySt *hash_next; /* chaining for hash table */
} CacheEntry;

typedef struct {
    CacheEntry *head;               /* MRU (most recently used) */
    CacheEntry *tail;               /* LRU (least recently used) */
    CacheEntry *hash[CACHE_HASH_SIZE]; /* hash table buckets */
    int        capacity;            /* max number of entries */
    int        size;                /* current entry count */
    int64_t    hits;                /* cache hit counter */
    int64_t    misses;              /* cache miss counter */
    int64_t    evictions;           /* total evictions */
    int        default_ttl;         /* default TTL in seconds, 0=none */
} LRUCache;

/* Initialize cache with given capacity */
void  cache_init(LRUCache *cache, int capacity, int default_ttl);

/* L5: O(1) average get. Returns value pointer (internal, do not free)
 * or NULL if key not found or expired. Updates LRU order. */
const char *cache_get(LRUCache *cache, const char *key);

/* L5: O(1) average put. Evicts LRU if at capacity. Returns 0 ok, -1 error. */
int  cache_put(LRUCache *cache, const char *key, const char *value, int ttl_sec);

/* Delete a key. Returns 0 if found, -1 if not found. */
int  cache_delete(LRUCache *cache, const char *key);

/* Check if key exists and is not expired (does not update LRU) */
bool cache_has(const LRUCache *cache, const char *key);

/* Remove all expired entries (active cleanup) */
int  cache_evict_expired(LRUCache *cache);

/* Evict the least recently used entry */
int  cache_evict_lru(LRUCache *cache);

/* Get current size */
int  cache_size(const LRUCache *cache);

/* Clear all entries */
void cache_clear(LRUCache *cache);

/* Destroy cache and free all memory */
void cache_destroy(LRUCache *cache);

/* Get hit rate (0.0 to 1.0) */
double cache_hit_rate(const LRUCache *cache);

/* Get statistics */
int64_t cache_hits(const LRUCache *cache);
int64_t cache_misses(const LRUCache *cache);
int64_t cache_evictions(const LRUCache *cache);

#endif
