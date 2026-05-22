#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define RATE_MAX_BUCKETS        4096
#define RATE_MAX_WINDOWS        4096
#define RATE_MAX_KEY_LEN        128
#define RATE_MAX_HEADER_LEN     64

typedef enum {
    RATE_ALGO_FIXED_WINDOW = 0,
    RATE_ALGO_SLIDING_WINDOW,
    RATE_ALGO_TOKEN_BUCKET
} RateAlgo;

typedef enum {
    RATE_SCOPE_USER = 0,
    RATE_SCOPE_IP,
    RATE_SCOPE_API,
    RATE_SCOPE_GLOBAL
} RateScope;

typedef struct {
    time_t window_start;
    uint32_t counter;
} FixedWindowEntry;

typedef struct {
    time_t timestamps[256];
    uint32_t head;
    uint32_t count;
} SlidingWindowEntry;

typedef struct {
    double tokens;
    double max_tokens;
    double refill_rate;
    time_t last_refill;
} TokenBucketEntry;

typedef struct {
    char key[RATE_MAX_KEY_LEN];
    RateScope scope;
    RateAlgo  algorithm;
    union {
        FixedWindowEntry   fixed;
        SlidingWindowEntry sliding;
        TokenBucketEntry   bucket;
    } state;
    uint32_t limit;
    uint32_t window_seconds;
    time_t created_at;
} RateLimitEntry;

typedef struct {
    RateLimitEntry entries[RATE_MAX_BUCKETS];
    size_t entry_count;
    uint32_t default_limit;
    uint32_t default_window_seconds;
} RateLimiter;

typedef struct {
    int allowed;
    uint32_t limit;
    uint32_t remaining;
    uint32_t reset_seconds;
    time_t reset_time;
} RateLimitResult;

void rate_limiter_init(RateLimiter *rl, uint32_t default_limit,
                       uint32_t window_seconds);

RateLimitEntry *rate_get_or_create_entry(RateLimiter *rl, const char *key,
                                          RateScope scope, RateAlgo algo,
                                          uint32_t limit, uint32_t window_seconds);

RateLimitResult rate_check_fixed_window(RateLimitEntry *entry);
RateLimitResult rate_check_sliding_window(RateLimitEntry *entry);
RateLimitResult rate_check_token_bucket(RateLimitEntry *entry);

RateLimitResult rate_limiter_check(RateLimiter *rl, const char *key,
                                   RateScope scope, RateAlgo algo,
                                   uint32_t limit, uint32_t window_seconds);

int rate_limiter_check_multi(RateLimiter *rl,
                             const char *keys[], RateScope scopes[],
                             RateAlgo algos[], uint32_t limits[],
                             uint32_t windows[], size_t count,
                             RateLimitResult results[]);

void rate_generate_headers(const RateLimitResult *result,
                           char *limit_hdr, size_t lh_size,
                           char *remaining_hdr, size_t rh_size,
                           char *reset_hdr, size_t rs_size);

int rate_redis_sim_acquire(const char *key, uint32_t limit,
                           uint32_t window_seconds);
int rate_redis_sim_release(const char *key);

int rate_lua_sim_check(const char *key, uint32_t limit,
                       uint32_t window_seconds,
                       uint32_t *remaining_out, time_t *reset_out);

void rate_limiter_cleanup(RateLimiter *rl);
void rate_dump_entry(const RateLimitEntry *entry);

#endif
