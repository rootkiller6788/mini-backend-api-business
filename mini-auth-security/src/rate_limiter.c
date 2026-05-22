#include "rate_limiter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void rate_limiter_init(RateLimiter *rl, uint32_t default_limit,
                       uint32_t window_seconds) {
    memset(rl, 0, sizeof(*rl));
    rl->default_limit = default_limit;
    rl->default_window_seconds = window_seconds;
}

RateLimitEntry *rate_get_or_create_entry(RateLimiter *rl, const char *key,
                                          RateScope scope, RateAlgo algo,
                                          uint32_t limit, uint32_t window_seconds) {
    size_t i;
    for (i = 0; i < rl->entry_count; i++) {
        if (strcmp(rl->entries[i].key, key) == 0 && rl->entries[i].scope == scope) {
            return &rl->entries[i];
        }
    }
    if (rl->entry_count >= RATE_MAX_BUCKETS) return NULL;

    i = rl->entry_count;
    strncpy(rl->entries[i].key, key, RATE_MAX_KEY_LEN - 1);
    rl->entries[i].scope = scope;
    rl->entries[i].algorithm = algo;
    rl->entries[i].limit = limit;
    rl->entries[i].window_seconds = window_seconds;
    rl->entries[i].created_at = time(NULL);

    switch (algo) {
    case RATE_ALGO_FIXED_WINDOW:
        rl->entries[i].state.fixed.window_start = time(NULL);
        rl->entries[i].state.fixed.counter = 0;
        break;
    case RATE_ALGO_SLIDING_WINDOW:
        memset(rl->entries[i].state.sliding.timestamps, 0,
               sizeof(rl->entries[i].state.sliding.timestamps));
        rl->entries[i].state.sliding.head = 0;
        rl->entries[i].state.sliding.count = 0;
        break;
    case RATE_ALGO_TOKEN_BUCKET:
        rl->entries[i].state.bucket.tokens = (double)limit;
        rl->entries[i].state.bucket.max_tokens = (double)limit;
        rl->entries[i].state.bucket.refill_rate = (double)limit / (double)window_seconds;
        rl->entries[i].state.bucket.last_refill = time(NULL);
        break;
    }
    rl->entry_count++;
    return &rl->entries[i];
}

RateLimitResult rate_check_fixed_window(RateLimitEntry *entry) {
    RateLimitResult res = {0};
    time_t now = time(NULL);
    time_t window_end = entry->state.fixed.window_start + entry->window_seconds;

    if (now >= window_end) {
        entry->state.fixed.window_start = now;
        entry->state.fixed.counter = 0;
    }

    res.limit = entry->limit;
    res.remaining = (entry->state.fixed.counter < entry->limit)
                    ? entry->limit - entry->state.fixed.counter - 1 : 0;
    res.allowed = (entry->state.fixed.counter < entry->limit) ? 1 : 0;
    res.reset_time = entry->state.fixed.window_start + entry->window_seconds;
    res.reset_seconds = (uint32_t)(res.reset_time - now);

    if (res.allowed) {
        entry->state.fixed.counter++;
    }
    return res;
}

RateLimitResult rate_check_sliding_window(RateLimitEntry *entry) {
    RateLimitResult res = {0};
    time_t now = time(NULL);
    time_t cutoff = now - entry->window_seconds;
    uint32_t keep_count = 0;
    uint32_t i;

    for (i = 0; i < entry->state.sliding.count; i++) {
        uint32_t idx = (entry->state.sliding.head + entry->state.sliding.count - 1 - i) % 256;
        if (entry->state.sliding.timestamps[idx] >= cutoff) {
            keep_count++;
        }
    }

    res.limit = entry->limit;
    res.remaining = (keep_count < entry->limit) ? entry->limit - keep_count - 1 : 0;
    res.allowed = (keep_count < entry->limit) ? 1 : 0;
    res.reset_time = now + entry->window_seconds;
    res.reset_seconds = entry->window_seconds;

    if (res.allowed) {
        entry->state.sliding.timestamps[entry->state.sliding.head] = now;
        entry->state.sliding.head = (entry->state.sliding.head + 1) % 256;
        if (entry->state.sliding.count < 256) entry->state.sliding.count++;
    }
    return res;
}

RateLimitResult rate_check_token_bucket(RateLimitEntry *entry) {
    RateLimitResult res = {0};
    time_t now = time(NULL);
    double elapsed = (double)(now - entry->state.bucket.last_refill);
    double new_tokens = elapsed * entry->state.bucket.refill_rate;

    entry->state.bucket.tokens += new_tokens;
    if (entry->state.bucket.tokens > entry->state.bucket.max_tokens) {
        entry->state.bucket.tokens = entry->state.bucket.max_tokens;
    }
    entry->state.bucket.last_refill = now;

    res.limit = entry->limit;
    res.allowed = (entry->state.bucket.tokens >= 1.0) ? 1 : 0;
    res.remaining = (uint32_t)fmax(0.0, entry->state.bucket.tokens - 1.0);
    res.reset_time = now + entry->window_seconds;
    res.reset_seconds = entry->window_seconds;

    if (res.allowed) {
        entry->state.bucket.tokens -= 1.0;
    }
    return res;
}

RateLimitResult rate_limiter_check(RateLimiter *rl, const char *key,
                                   RateScope scope, RateAlgo algo,
                                   uint32_t limit, uint32_t window_seconds) {
    RateLimitEntry *entry = rate_get_or_create_entry(rl, key, scope, algo,
                                                      limit, window_seconds);
    if (!entry) {
        RateLimitResult deny = {0, limit, 0, window_seconds, 0};
        return deny;
    }

    switch (algo) {
    case RATE_ALGO_FIXED_WINDOW:
        return rate_check_fixed_window(entry);
    case RATE_ALGO_SLIDING_WINDOW:
        return rate_check_sliding_window(entry);
    case RATE_ALGO_TOKEN_BUCKET:
        return rate_check_token_bucket(entry);
    default:
        return rate_check_fixed_window(entry);
    }
}

int rate_limiter_check_multi(RateLimiter *rl,
                             const char *keys[], RateScope scopes[],
                             RateAlgo algos[], uint32_t limits[],
                             uint32_t windows[], size_t count,
                             RateLimitResult results[]) {
    size_t i;
    int all_allowed = 1;
    for (i = 0; i < count; i++) {
        results[i] = rate_limiter_check(rl, keys[i], scopes[i], algos[i],
                                        limits[i], windows[i]);
        if (!results[i].allowed) all_allowed = 0;
    }
    return all_allowed;
}

void rate_generate_headers(const RateLimitResult *result,
                           char *limit_hdr, size_t lh_size,
                           char *remaining_hdr, size_t rh_size,
                           char *reset_hdr, size_t rs_size) {
    snprintf(limit_hdr, lh_size, "%u", result->limit);
    snprintf(remaining_hdr, rh_size, "%u", result->remaining);
    snprintf(reset_hdr, rs_size, "%u", result->reset_seconds);
}

int rate_redis_sim_acquire(const char *key, uint32_t limit,
                           uint32_t window_seconds) {
    static struct {
        char key[RATE_MAX_KEY_LEN];
        uint32_t counter;
        time_t window_start;
        time_t created_at;
    } sim_store[256];
    static size_t sim_count = 0;
    size_t i;
    time_t now = time(NULL);

    for (i = 0; i < sim_count; i++) {
        if (strcmp(sim_store[i].key, key) == 0) {
            if (now - sim_store[i].window_start >= window_seconds) {
                sim_store[i].window_start = now;
                sim_store[i].counter = 0;
            }
            if (sim_store[i].counter < limit) {
                sim_store[i].counter++;
                return 1;
            }
            return 0;
        }
    }
    if (sim_count < 256) {
        strncpy(sim_store[sim_count].key, key, RATE_MAX_KEY_LEN - 1);
        sim_store[sim_count].counter = 1;
        sim_store[sim_count].window_start = now;
        sim_store[sim_count].created_at = now;
        sim_count++;
        return 1;
    }
    return 0;
}

int rate_redis_sim_release(const char *key) {
    static char stored_keys[256][RATE_MAX_KEY_LEN];
    static uint32_t stored_counters[256];
    static size_t store_count = 0;
    size_t i;
    (void)key;

    for (i = 0; i < 256; i++) {
        stored_counters[i] = 0;
    }
    store_count = 0;
    return 0;
}

int rate_lua_sim_check(const char *key, uint32_t limit,
                       uint32_t window_seconds,
                       uint32_t *remaining_out, time_t *reset_out) {
    return rate_redis_sim_acquire(key, limit, window_seconds);
}

void rate_limiter_cleanup(RateLimiter *rl) {
    size_t i = 0;
    time_t now = time(NULL);
    while (i < rl->entry_count) {
        if (now - rl->entries[i].created_at > 86400) {
            if (i < rl->entry_count - 1) {
                rl->entries[i] = rl->entries[rl->entry_count - 1];
            }
            rl->entry_count--;
        } else {
            i++;
        }
    }
}

void rate_dump_entry(const RateLimitEntry *entry) {
    printf("Key=%s Scope=%d Algo=%d Limit=%u Window=%u\n",
           entry->key, entry->scope, entry->algorithm,
           entry->limit, entry->window_seconds);
}
