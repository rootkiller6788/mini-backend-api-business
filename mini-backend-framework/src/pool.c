/*
 * pool.c �� Object Pool Implementation
 *
 * L3: Bounded resource pool with acquire/release semantics.
 * Each slot tracks in_use state, last_used timestamp, and use_count.
 *
 * L5: Blocking acquire with timeout �� uses exponential backoff
 * polling (simplified; production would use condition variables).
 * Timeout_ms == -1 means infinite wait.
 *
 * L4: Little's Law: L = lambda * W.
 * For a stable pool: pool_size >= avg_concurrent_requests * avg_hold_time.
 *
 * Reference: Michael T. Nygard, "Release It!" (2007) Ch. 2 �� Pools;
 * HikariCP documentation; Apache Commons Pool.
 */

#include "pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Sleep for ms milliseconds (busy-wait fallback if nanosleep unavailable) */
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static double seconds_since(const struct timespec *t) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double d = (double)(now.tv_sec - t->tv_sec);
    d += (double)(now.tv_nsec - t->tv_nsec) / 1e9;
    return d;
}

void pool_init(Pool *pool, int capacity, int idle_timeout_sec,
               const char *url, PoolCreator create_fn,
               PoolDestroyer destroy_fn, PoolValidator validate_fn,
               void *create_arg) {
    if (!pool) return;
    memset(pool, 0, sizeof(Pool));
    pool->capacity = capacity > 0 ? (capacity < POOL_MAX_RESOURCES ? capacity : POOL_MAX_RESOURCES) : 8;
    pool->idle_timeout_sec = idle_timeout_sec;
    if (url) {
        strncpy(pool->url, url, POOL_MAX_URL - 1);
        pool->url[POOL_MAX_URL - 1] = '\0';
    }
    pool->create_fn = create_fn;
    pool->destroy_fn = destroy_fn;
    pool->validate_fn = validate_fn;
    pool->create_arg = create_arg;
    pool->initialized = true;
}

/* Find an idle, valid slot. Creates new resource if pool not full. */
static int find_or_create_slot(Pool *pool) {
    int i;

    /* First, look for an idle slot with a valid existing resource */
    for (i = 0; i < pool->capacity; i++) {
        if (!pool->slots[i].in_use && pool->slots[i].resource) {
            /* Validate before reuse */
            if (!pool->validate_fn || pool->validate_fn(pool->slots[i].resource)) {
                return i;
            }
            /* Resource is stale �� destroy it */
            if (pool->destroy_fn) pool->destroy_fn(pool->slots[i].resource);
            pool->slots[i].resource = NULL;
            pool->idle_count--;
        }
    }

    /* Look for an empty slot (never used) */
    for (i = 0; i < pool->capacity; i++) {
        if (!pool->slots[i].resource) {
            return i;
        }
    }

    return -1; /* pool full */
}

void *pool_acquire(Pool *pool, int timeout_ms) {
    int idx, waited = 0;
    int poll_interval = 10; /* ms between retries */

    if (!pool || !pool->initialized || !pool->create_fn) return NULL;

    while (1) {
        idx = find_or_create_slot(pool);

        if (idx >= 0) {
            /* Create new resource if slot is empty */
            if (!pool->slots[idx].resource) {
                pool->slots[idx].resource = pool->create_fn(pool->url, pool->create_arg);
                if (!pool->slots[idx].resource) {
                    pool->total_failures++;
                    return NULL;
                }
            }

            pool->slots[idx].in_use = true;
            pool->slots[idx].use_count++;
            pool->in_use++;
            pool->total_acquired++;
            return pool->slots[idx].resource;
        }

        /* Pool full �� wait and retry */
        if (timeout_ms >= 0 && waited >= timeout_ms) {
            pool->total_timeouts++;
            return NULL;
        }

        sleep_ms(poll_interval);
        if (timeout_ms >= 0) waited += poll_interval;

        /* Exponential backoff: double interval up to 100ms */
        if (poll_interval < 100) poll_interval *= 2;
    }
}

int pool_release(Pool *pool, void *resource) {
    int i;

    if (!pool || !resource) return -1;

    for (i = 0; i < pool->capacity; i++) {
        if (pool->slots[i].resource == resource && pool->slots[i].in_use) {
            pool->slots[i].in_use = false;
            clock_gettime(CLOCK_MONOTONIC, &pool->slots[i].last_used);
            pool->in_use--;
            pool->idle_count++;
            pool->total_released++;
            return 0;
        }
    }
    return -1;
}

int pool_sweep_idle(Pool *pool) {
    int i, closed = 0;

    if (!pool || pool->idle_timeout_sec <= 0) return 0;

    for (i = 0; i < pool->capacity; i++) {
        if (!pool->slots[i].in_use && pool->slots[i].resource) {
            double idle_sec = seconds_since(&pool->slots[i].last_used);
            if (idle_sec >= (double)pool->idle_timeout_sec) {
                if (pool->destroy_fn) pool->destroy_fn(pool->slots[i].resource);
                pool->slots[i].resource = NULL;
                pool->idle_count--;
                closed++;
            }
        }
    }
    return closed;
}

int pool_available(const Pool *pool) {
    if (!pool) return 0;
    return pool->capacity - pool->in_use;
}

int pool_in_use(const Pool *pool) {
    return pool ? pool->in_use : 0;
}

int pool_capacity(const Pool *pool) {
    return pool ? pool->capacity : 0;
}

int pool_prewarm(Pool *pool, int min_idle) {
    int created = 0;
    int i;

    if (!pool || !pool->create_fn) return 0;

    for (i = 0; i < pool->capacity && created < min_idle; i++) {
        if (!pool->slots[i].resource) {
            pool->slots[i].resource = pool->create_fn(pool->url, pool->create_arg);
            if (pool->slots[i].resource) {
                pool->slots[i].in_use = false;
                clock_gettime(CLOCK_MONOTONIC, &pool->slots[i].last_used);
                pool->idle_count++;
                created++;
            }
        }
    }
    return created;
}

void pool_destroy(Pool *pool) {
    int i;
    if (!pool) return;

    for (i = 0; i < pool->capacity; i++) {
        if (pool->slots[i].resource) {
            if (pool->destroy_fn) pool->destroy_fn(pool->slots[i].resource);
            pool->slots[i].resource = NULL;
        }
    }
    memset(pool, 0, sizeof(Pool));
}
