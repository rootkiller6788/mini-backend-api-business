#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

/*
 * L1: Core Definitions — Rate Limiter
 *
 * Rate limiting controls the frequency of operations. Used in API
 * gateways, network traffic shaping, and DDoS protection.
 *
 * L4: Token Bucket Theorem — defined by two parameters:
 *   r = token refill rate (tokens/sec)
 *   b = bucket capacity (maximum burst size)
 * Long-term average rate ≤ r; short-term burst ≤ b.
 * Reference: Turner, "New Directions in Communications" (1986).
 *
 * L5: Two algorithms implemented:
 *   1. Token Bucket — classic traffic shaping
 *   2. Sliding Window Log — precise per-request tracking
 */

#define RL_MAX_KEY_LEN   64
#define RL_MAX_BUCKETS   256

/*
 * L2: Token Bucket — tokens are added at constant rate r up to
 * capacity b. Each request consumes 1 token. If no token available,
 * request is rejected. O(1) per check.
 */
typedef struct {
    double       tokens;          /* current token count */
    double       rate;            /* refill rate: tokens per second */
    double       capacity;        /* max tokens (burst size) */
    struct timespec last_refill;  /* timestamp of last token addition */
    int64_t      total_requests;  /* counter: total allowed requests */
    int64_t      total_rejected;  /* counter: total rejected requests */
    bool         initialized;
} TokenBucket;

void  tb_init(TokenBucket *tb, double rate, double capacity);
bool  tb_consume(TokenBucket *tb);           /* returns true if allowed */
int   tb_consume_n(TokenBucket *tb, int n);  /* consume n tokens, returns allowed count */
void  tb_reset(TokenBucket *tb);
int64_t tb_requests(const TokenBucket *tb);
int64_t tb_rejected(const TokenBucket *tb);

/* Get current token count (for monitoring) */
double tb_tokens(const TokenBucket *tb);

/*
 * L5: Sliding Window Log — maintains timestamps of recent requests.
 * On each request, evict entries older than window_size.
 * Rejects if count exceeds max_requests.
 * Complexity: O(k) where k = requests in current window.
 * More memory-intensive but more precise than token bucket.
 */
typedef struct {
    struct timespec timestamps[4096];  /* ring buffer of request times */
    int            head;               /* write index */
    int            count;              /* total entries in buffer */
    double         window_sec;         /* window duration in seconds */
    int            max_requests;       /* max requests per window */
    int64_t        total_allowed;
    int64_t        total_rejected;
} SlidingWindow;

void sw_init(SlidingWindow *sw, double window_sec, int max_requests);
bool sw_allow(SlidingWindow *sw);            /* returns true if allowed */
int  sw_available(const SlidingWindow *sw);  /* remaining capacity */
void sw_reset(SlidingWindow *sw);

/*
 * L3: Composite Rate Limiter — combines token bucket (burst) and
 * sliding window (sustained) for defense-in-depth rate limiting.
 */
typedef struct {
    TokenBucket    burst;     /* burst protection */
    SlidingWindow  sustained; /* sustained rate protection */
    bool           reject_on_burst;
    bool           reject_on_window;
} CompositeLimiter;

void cl_init(CompositeLimiter *cl, double burst_rate, double burst_cap,
             double window_sec, int max_per_window);
bool cl_allow(CompositeLimiter *cl);

#endif
