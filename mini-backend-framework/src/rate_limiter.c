/*
 * rate_limiter.c — Rate Limiter Implementation
 *
 * L4: Token Bucket Theorem (Turner, 1986):
 *   For any time T, the total bytes sent cannot exceed r*T + b,
 *   where r = token rate, b = bucket capacity.
 *   This bounds both average rate (r) and burst size (b).
 *
 * L5: Token Bucket Algorithm:
 *   On each request:
 *     1. Calculate elapsed time since last refill
 *     2. Add elapsed * rate tokens (capped at capacity)
 *     3. If tokens >= cost, consume and allow; else reject
 *   Complexity: O(1) amortized per request.
 *
 * L5: Sliding Window Log:
 *   Maintains a log of request timestamps. On each check:
 *     1. Evict all entries with age > window_size
 *     2. If count < max_requests, add current time and allow
 *   Complexity: O(k) per request where k = entries in window.
 *
 * Reference: Andrew S. Tanenbaum, "Computer Networks" §5.4.3;
 * Nginx rate limiting documentation.
 */

#include "rate_limiter.h"
#include <string.h>
#include <time.h>

/* Helper: get elapsed seconds since a timespec */
static double elapsed_sec(const struct timespec *since, const struct timespec *now) {
    double d = (double)(now->tv_sec - since->tv_sec);
    d += (double)(now->tv_nsec - since->tv_nsec) / 1e9;
    return d;
}

/* ============ Token Bucket ============ */

void tb_init(TokenBucket *tb, double rate, double capacity) {
    if (!tb) return;
    memset(tb, 0, sizeof(TokenBucket));
    tb->rate = rate > 0 ? rate : 1.0;
    tb->capacity = capacity > 0 ? capacity : tb->rate;
    tb->tokens = tb->capacity;   /* start full */
    clock_gettime(CLOCK_MONOTONIC, &tb->last_refill);
    tb->initialized = true;
}

static void tb_refill(TokenBucket *tb) {
    struct timespec now;
    double elapsed, new_tokens;

    if (!tb || !tb->initialized) return;

    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed = elapsed_sec(&tb->last_refill, &now);
    if (elapsed <= 0) return;

    new_tokens = elapsed * tb->rate;
    tb->tokens += new_tokens;
    if (tb->tokens > tb->capacity) tb->tokens = tb->capacity;
    tb->last_refill = now;
}

bool tb_consume(TokenBucket *tb) {
    return tb_consume_n(tb, 1) == 1;
}

int tb_consume_n(TokenBucket *tb, int n) {
    int allowed;
    if (!tb || !tb->initialized || n <= 0) return 0;

    tb_refill(tb);
    allowed = (tb->tokens >= (double)n) ? n : (int)tb->tokens;

    if (allowed > 0) {
        tb->tokens -= (double)allowed;
        tb->total_requests += allowed;
    }
    tb->total_rejected += (n - allowed);

    return allowed;
}

void tb_reset(TokenBucket *tb) {
    if (!tb) return;
    tb->tokens = tb->capacity;
    clock_gettime(CLOCK_MONOTONIC, &tb->last_refill);
    tb->total_requests = 0;
    tb->total_rejected = 0;
}

double tb_tokens(const TokenBucket *tb) {
    TokenBucket *mut = (TokenBucket *)tb;
    if (!tb || !tb->initialized) return 0.0;
    tb_refill(mut);
    return tb->tokens;
}

int64_t tb_requests(const TokenBucket *tb) {
    return tb ? tb->total_requests : 0;
}

int64_t tb_rejected(const TokenBucket *tb) {
    return tb ? tb->total_rejected : 0;
}

/* ============ Sliding Window Log ============ */

void sw_init(SlidingWindow *sw, double window_sec, int max_requests) {
    if (!sw) return;
    memset(sw, 0, sizeof(SlidingWindow));
    sw->window_sec = window_sec > 0 ? window_sec : 1.0;
    sw->max_requests = max_requests > 0 ? max_requests : 1;
}

/*
 * L5: Sliding window eviction — removes timestamps older than window.
 * Uses ring buffer semantics: head wraps around modulo buffer size.
 * Since timestamps are monotonically increasing, we can stop early.
 */
static void sw_evict(SlidingWindow *sw) {
    struct timespec now;
    double age;
    int i, kept = 0;
    struct timespec kept_times[4096];

    clock_gettime(CLOCK_MONOTONIC, &now);

    /* Collect entries still within the window */
    for (i = 0; i < sw->count; i++) {
        int idx = (sw->head - sw->count + i + 4096) % 4096;
        age = elapsed_sec(&sw->timestamps[idx], &now);
        if (age < sw->window_sec) {
            kept_times[kept++] = sw->timestamps[idx];
        }
    }

    /* Rebuild buffer with kept entries */
    for (i = 0; i < kept; i++) {
        sw->timestamps[i] = kept_times[i];
    }
    sw->head = kept % 4096;
    sw->count = kept;
}

bool sw_allow(SlidingWindow *sw) {
    if (!sw) return false;

    sw_evict(sw);

    if (sw->count < sw->max_requests) {
        clock_gettime(CLOCK_MONOTONIC, &sw->timestamps[sw->head]);
        sw->head = (sw->head + 1) % 4096;
        sw->count++;
        sw->total_allowed++;
        return true;
    }

    sw->total_rejected++;
    return false;
}

int sw_available(const SlidingWindow *sw) {
    SlidingWindow *mut = (SlidingWindow *)sw;
    if (!sw) return 0;
    sw_evict(mut);
    return sw->max_requests - sw->count;
}

void sw_reset(SlidingWindow *sw) {
    if (!sw) return;
    sw->head = 0;
    sw->count = 0;
    sw->total_allowed = 0;
    sw->total_rejected = 0;
}

/* ============ Composite Limiter ============ */

/*
 * L3: Defense-in-depth — token bucket handles burst protection
 * while sliding window enforces sustained rate limits.
 * Both must agree to allow the request.
 */
void cl_init(CompositeLimiter *cl, double burst_rate, double burst_cap,
             double window_sec, int max_per_window) {
    if (!cl) return;
    tb_init(&cl->burst, burst_rate, burst_cap);
    sw_init(&cl->sustained, window_sec, max_per_window);
    cl->reject_on_burst = false;
    cl->reject_on_window = false;
}

bool cl_allow(CompositeLimiter *cl) {
    bool burst_ok, window_ok;

    if (!cl) return false;

    cl->reject_on_burst = false;
    cl->reject_on_window = false;

    burst_ok = tb_consume(&cl->burst);
    window_ok = sw_allow(&cl->sustained);

    if (!burst_ok) cl->reject_on_burst = true;
    if (!window_ok) cl->reject_on_window = true;

    return burst_ok && window_ok;
}
