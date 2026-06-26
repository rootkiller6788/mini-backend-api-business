#include "rate_limiter.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * L4 Theorem: Token Bucket (Turner 1986)
 *   Let r be the token rate (tokens/sec) and b the burst size.
 *   Over any time interval T, the number of tokens consumed C satisfies:
 *     C ≤ r * T + b
 *   This bounds burstiness while maintaining average rate.
 *
 * L4 Theorem: Leaky Bucket
 *   Output rate is strictly bounded by rate r. Input queue depth
 *   bounded by capacity. Converts GCRA (Generic Cell Rate Algorithm)
 *   into continuous-time equivalent.
 *   Overflow occurs when: arrival_rate > r for sustained interval.
 */

/* ==================== Token Bucket ==================== */

void rl_token_refill(rl_token_bucket_t *tb)
{
    time_t now = time(NULL);
    if (now <= tb->last_refill) {
        if (now < tb->last_refill) now = tb->last_refill;
        return;
    }

    double elapsed = difftime(now, tb->last_refill);
    tb->tokens += tb->rate * elapsed;
    if (tb->tokens > tb->burst)
        tb->tokens = tb->burst;
    tb->last_refill = now;
}

void rl_token_bucket_init(rate_limiter_t *rl, double rate, double burst)
{
    if (!rl) return;
    memset(rl, 0, sizeof(*rl));
    rl->type = RL_TYPE_TOKEN_BUCKET;
    rl->impl.token.rate  = rate > 0 ? rate : 1.0;
    rl->impl.token.burst = burst > rate ? burst : rate;
    rl->impl.token.tokens = rl->impl.token.burst;
    rl->impl.token.last_refill = time(NULL);
}

static int token_consume(rate_limiter_t *rl, double cost, int consume)
{
    rl_token_bucket_t *tb = &rl->impl.token;
    rl_token_refill(tb);

    if (tb->tokens >= cost) {
        if (consume) tb->tokens -= cost;
        return 1;
    }
    return 0;
}

static double token_time_until_next(const rate_limiter_t *rl, double cost)
{
    rl_token_bucket_t *tb = (rl_token_bucket_t *)&rl->impl.token;
    double deficit = cost - tb->tokens;
    if (deficit <= 0) return 0;
    return deficit / tb->rate;
}

static double token_usage_ratio(const rate_limiter_t *rl)
{
    return 1.0 - rl->impl.token.tokens / rl->impl.token.burst;
}

static double token_remaining(const rate_limiter_t *rl)
{
    return rl->impl.token.tokens;
}

/* ==================== Leaky Bucket ==================== */

void rl_leaky_drain(rl_leaky_bucket_t *lb)
{
    time_t now = time(NULL);
    if (now <= lb->last_leak) return;

    double elapsed = difftime(now, lb->last_leak);
    lb->water_level -= lb->rate * elapsed;
    if (lb->water_level < 0)
        lb->water_level = 0;
    lb->last_leak = now;
}

void rl_leaky_bucket_init(rate_limiter_t *rl, double rate, double capacity)
{
    if (!rl) return;
    memset(rl, 0, sizeof(*rl));
    rl->type = RL_TYPE_LEAKY_BUCKET;
    rl->impl.leaky.rate     = rate > 0 ? rate : 1.0;
    rl->impl.leaky.capacity = capacity > 0 ? capacity : rate;
    rl->impl.leaky.water_level = 0;
    rl->impl.leaky.last_leak   = time(NULL);
}

static int leaky_consume(rate_limiter_t *rl, double cost, int consume)
{
    rl_leaky_bucket_t *lb = &rl->impl.leaky;
    rl_leaky_drain(lb);

    if (lb->water_level + cost <= lb->capacity) {
        if (consume) lb->water_level += cost;
        return 1;
    }
    return 0;
}

static double leaky_time_until_next(const rate_limiter_t *rl, double cost)
{
    rl_leaky_bucket_t *lb = (rl_leaky_bucket_t *)&rl->impl.leaky;
    double available = lb->capacity - lb->water_level;
    if (cost <= available) return 0;
    double excess = cost - available;
    return excess / lb->rate;
}

static double leaky_usage_ratio(const rate_limiter_t *rl)
{
    return rl->impl.leaky.water_level / rl->impl.leaky.capacity;
}

static double leaky_remaining(const rate_limiter_t *rl)
{
    return rl->impl.leaky.capacity - rl->impl.leaky.water_level;
}

/* ==================== Sliding Window ==================== */

void rl_sliding_prune(rl_sliding_window_t *sw)
{
    time_t now = time(NULL);
    time_t cutoff = (time_t)(now - (time_t)sw->window_sec);
    if (cutoff < 0) cutoff = 0;

    while (sw->count > 0) {
        rl_window_entry_t *entry = &sw->entries[sw->head];
        if (entry->timestamp >= cutoff) break;
        sw->current_cost -= entry->cost;
        sw->head = (sw->head + 1) % 256;
        sw->count--;
    }

    if (sw->current_cost < 0) sw->current_cost = 0;
}

void rl_sliding_window_init(rate_limiter_t *rl, double window_sec, double max_cost)
{
    if (!rl) return;
    memset(rl, 0, sizeof(*rl));
    rl->type = RL_TYPE_SLIDING_WINDOW;
    rl->impl.sliding.window_sec   = window_sec > 0 ? window_sec : 1.0;
    rl->impl.sliding.max_cost     = max_cost > 0 ? max_cost : 1.0;
    rl->impl.sliding.current_cost = 0;
    rl->impl.sliding.count        = 0;
    rl->impl.sliding.head         = 0;
}

static int sliding_consume(rate_limiter_t *rl, double cost, int consume)
{
    rl_sliding_window_t *sw = &rl->impl.sliding;
    rl_sliding_prune(sw);

    if (sw->current_cost + cost <= sw->max_cost) {
        if (consume) {
            int tail = (sw->head + sw->count) % 256;
            sw->entries[tail].timestamp = time(NULL);
            sw->entries[tail].cost      = cost;
            sw->current_cost += cost;
            sw->count++;
        }
        return 1;
    }
    return 0;
}

static double sliding_time_until_next(const rate_limiter_t *rl, double cost)
{
    rl_sliding_window_t *sw = (rl_sliding_window_t *)&rl->impl.sliding;
    (void)rl; /* suppress unused warning when compiled with -Wextra */
    if (sw->current_cost + cost <= sw->max_cost) return 0;
    if (sw->count == 0) return 0;
    double time_to_expire = difftime(sw->entries[sw->head].timestamp
                                     + (time_t)sw->window_sec,
                                     time(NULL));
    if (time_to_expire < 0) time_to_expire = 0;
    return time_to_expire;
}

static double sliding_usage_ratio(const rate_limiter_t *rl)
{
    return rl->impl.sliding.current_cost / rl->impl.sliding.max_cost;
}

static double sliding_remaining(const rate_limiter_t *rl)
{
    return rl->impl.sliding.max_cost - rl->impl.sliding.current_cost;
}

/* ==================== Fixed Window ==================== */

void rl_fixed_window_init(rate_limiter_t *rl, double window_sec, double max_count)
{
    if (!rl) return;
    memset(rl, 0, sizeof(*rl));
    rl->type = RL_TYPE_FIXED_WINDOW;
    rl->impl.fixed.window_sec  = window_sec > 0 ? window_sec : 1.0;
    rl->impl.fixed.max_count   = max_count > 0 ? max_count : 1.0;
    rl->impl.fixed.count       = 0;
    rl->impl.fixed.window_start = time(NULL);
}

static int fixed_consume(rate_limiter_t *rl, double cost, int consume)
{
    rl_fixed_window_t *fw = &rl->impl.fixed;
    time_t now = time(NULL);

    if (difftime(now, fw->window_start) >= fw->window_sec) {
        fw->window_start = now;
        fw->count = 0;
    }

    if (fw->count + cost <= fw->max_count) {
        if (consume) fw->count += cost;
        return 1;
    }
    return 0;
}

static double fixed_time_until_next(const rate_limiter_t *rl, double cost)
{
    rl_fixed_window_t *fw = (rl_fixed_window_t *)&rl->impl.fixed;
    (void)cost;
    if (fw->count < fw->max_count) return 0;
    return fw->window_sec - difftime(time(NULL), fw->window_start);
}

static double fixed_usage_ratio(const rate_limiter_t *rl)
{
    return rl->impl.fixed.count / rl->impl.fixed.max_count;
}

static double fixed_remaining(const rate_limiter_t *rl)
{
    return rl->impl.fixed.max_count - rl->impl.fixed.count;
}

/* ==================== Dispatcher ==================== */

int rl_consume(rate_limiter_t *rl, double cost)
{
    if (!rl) return 0;
    if (cost <= 0) return 1;

    switch (rl->type) {
        case RL_TYPE_TOKEN_BUCKET:
            rl_token_refill(&rl->impl.token);
            return token_consume(rl, cost, 1);
        case RL_TYPE_LEAKY_BUCKET:
            rl_leaky_drain(&rl->impl.leaky);
            return leaky_consume(rl, cost, 1);
        case RL_TYPE_SLIDING_WINDOW:
            rl_sliding_prune(&rl->impl.sliding);
            return sliding_consume(rl, cost, 1);
        case RL_TYPE_FIXED_WINDOW:
            return fixed_consume(rl, cost, 1);
        default:
            return 0;
    }
}

int rl_consume_nowait(rate_limiter_t *rl, double cost)
{
    if (!rl) return 0;
    if (cost <= 0) return 1;

    switch (rl->type) {
        case RL_TYPE_TOKEN_BUCKET:
            return token_consume(rl, cost, 0);
        case RL_TYPE_LEAKY_BUCKET:
            return leaky_consume(rl, cost, 0);
        case RL_TYPE_SLIDING_WINDOW:
            return sliding_consume(rl, cost, 0);
        case RL_TYPE_FIXED_WINDOW:
            return fixed_consume(rl, cost, 0);
        default:
            return 0;
    }
}

int rl_would_allow(const rate_limiter_t *rl, double cost)
{
    return rl_consume_nowait((rate_limiter_t *)rl, cost);
}

double rl_time_until_next(const rate_limiter_t *rl, double cost)
{
    if (!rl) return -1.0;
    switch (rl->type) {
        case RL_TYPE_TOKEN_BUCKET:   return token_time_until_next(rl, cost);
        case RL_TYPE_LEAKY_BUCKET:   return leaky_time_until_next(rl, cost);
        case RL_TYPE_SLIDING_WINDOW: return sliding_time_until_next(rl, cost);
        case RL_TYPE_FIXED_WINDOW:   return fixed_time_until_next(rl, cost);
        default: return -1.0;
    }
}

void rl_reset(rate_limiter_t *rl)
{
    if (!rl) return;
    switch (rl->type) {
        case RL_TYPE_TOKEN_BUCKET:
            rl->impl.token.tokens = rl->impl.token.burst;
            rl->impl.token.last_refill = time(NULL);
            break;
        case RL_TYPE_LEAKY_BUCKET:
            rl->impl.leaky.water_level = 0;
            rl->impl.leaky.last_leak   = time(NULL);
            break;
        case RL_TYPE_SLIDING_WINDOW:
            rl->impl.sliding.current_cost = 0;
            rl->impl.sliding.count = 0;
            rl->impl.sliding.head  = 0;
            break;
        case RL_TYPE_FIXED_WINDOW:
            rl->impl.fixed.count = 0;
            rl->impl.fixed.window_start = time(NULL);
            break;
        default: break;
    }
}

double rl_usage_ratio(const rate_limiter_t *rl)
{
    if (!rl) return 0;
    switch (rl->type) {
        case RL_TYPE_TOKEN_BUCKET:   return token_usage_ratio(rl);
        case RL_TYPE_LEAKY_BUCKET:   return leaky_usage_ratio(rl);
        case RL_TYPE_SLIDING_WINDOW: return sliding_usage_ratio(rl);
        case RL_TYPE_FIXED_WINDOW:   return fixed_usage_ratio(rl);
        default: return 0;
    }
}

double rl_remaining(const rate_limiter_t *rl)
{
    if (!rl) return 0;
    switch (rl->type) {
        case RL_TYPE_TOKEN_BUCKET:   return token_remaining(rl);
        case RL_TYPE_LEAKY_BUCKET:   return leaky_remaining(rl);
        case RL_TYPE_SLIDING_WINDOW: return sliding_remaining(rl);
        case RL_TYPE_FIXED_WINDOW:   return fixed_remaining(rl);
        default: return 0;
    }
}
