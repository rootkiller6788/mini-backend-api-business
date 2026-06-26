#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <stdint.h>
#include <time.h>

/*
 * L2 Core Concept: Rate limiting — controlling the rate of job execution
 * to protect downstream services and ensure fair resource allocation.
 *
 * L3 Engineering Structures:
 *   - Token Bucket: smooth bursts, configurable rate/capacity
 *   - Leaky Bucket: constant outflow, shapes traffic to a steady rate
 *   - Sliding Window Log: precise rate enforcement over a moving time window
 *   - Fixed Window Counter: simple approximate counting (cheap, but edge-bursty)
 *
 * L4 Standards/Theorems:
 *   - Token Bucket (Turner 1986): Allows bursts up to bucket capacity,
 *     enforces average rate r tokens/sec over any long interval.
 *   - Leaky Bucket (Turner 1986): Enforces strict constant output rate,
 *     converts bursty arrival into steady stream. Analogous to GCRA in ATM.
 *   - Little's Law: L = lambda * W  (applicable for queue sizing)
 *
 * L5 Algorithms:
 *   - Token bucket refill algorithm (O(1) per consume)
 *   - Sliding window log with expired entry pruning (amortized O(1))
 *
 * L8 Advanced Topics:
 *   - Distributed rate limiting (Redis-based / consistent hashing)
 *   - Hierarchical rate limiting (per-user + per-API + global budgets)
 *
 * Reference:
 *   - Turner, J. (1986) "New directions in communications"
 *   - MIT 6.829: Computer Networks (token bucket)
 *   - Google SRE Book Ch. 22: Addressing Cascading Failures
 */

/* Rate limiter types */
typedef enum {
    RL_TYPE_TOKEN_BUCKET   = 0,
    RL_TYPE_LEAKY_BUCKET   = 1,
    RL_TYPE_SLIDING_WINDOW = 2,
    RL_TYPE_FIXED_WINDOW   = 3
} rl_type_t;

/* Token bucket configuration */
typedef struct {
    double   rate;            /* tokens per second (sustained rate) */
    double   burst;           /* max tokens (burst capacity) */
    double   tokens;          /* current token count */
    time_t   last_refill;     /* timestamp of last refill */
} rl_token_bucket_t;

/* Leaky bucket configuration */
typedef struct {
    double   rate;            /* output rate (units per second) */
    double   capacity;        /* max queue size */
    double   water_level;     /* current queue depth */
    time_t   last_leak;       /* last leak timestamp */
} rl_leaky_bucket_t;

/* Sliding window entry */
typedef struct {
    time_t   timestamp;
    double   cost;
} rl_window_entry_t;

/* Sliding window configuration */
typedef struct {
    double           window_sec;    /* window duration in seconds */
    double           max_cost;      /* max total cost in window */
    double           current_cost;  /* total cost of entries in window */
    rl_window_entry_t entries[256];
    int              count;
    int              head;          /* index of oldest entry */
} rl_sliding_window_t;

/* Fixed window counter */
typedef struct {
    double           window_sec;
    double           max_count;
    double           count;
    time_t           window_start;
} rl_fixed_window_t;

/* Union rate limiter */
typedef struct {
    rl_type_t type;
    union {
        rl_token_bucket_t   token;
        rl_leaky_bucket_t   leaky;
        rl_sliding_window_t sliding;
        rl_fixed_window_t   fixed;
    } impl;
} rate_limiter_t;

/* Create rate limiters */
void rl_token_bucket_init(rate_limiter_t *rl, double rate, double burst);
void rl_leaky_bucket_init(rate_limiter_t *rl, double rate, double capacity);
void rl_sliding_window_init(rate_limiter_t *rl, double window_sec, double max_cost);
void rl_fixed_window_init(rate_limiter_t *rl, double window_sec, double max_count);

/* Consume (check if operation is allowed) */
int  rl_consume(rate_limiter_t *rl, double cost);
int  rl_consume_nowait(rate_limiter_t *rl, double cost);

/* Query (don't consume) */
int  rl_would_allow(const rate_limiter_t *rl, double cost);

/* Get time until next permitted (seconds) */
double rl_time_until_next(const rate_limiter_t *rl, double cost);

/* Reset limiter state */
void rl_reset(rate_limiter_t *rl);

/* Token bucket: refill tokens based on elapsed time.
 * O(1) algorithm: tokens += rate * delta_time, clamped to burst. */
void rl_token_refill(rl_token_bucket_t *tb);

/* Leaky bucket: drain water level based on elapsed time.
 * O(1) algorithm: water = max(0, water - rate * delta_time). */
void rl_leaky_drain(rl_leaky_bucket_t *lb);

/* Sliding window: prune expired entries.
 * Advances head pointer, amortized O(1). */
void rl_sliding_prune(rl_sliding_window_t *sw);

/* Get current usage ratio (0.0-1.0) */
double rl_usage_ratio(const rate_limiter_t *rl);

/* Get remaining capacity */
double rl_remaining(const rate_limiter_t *rl);

#endif
