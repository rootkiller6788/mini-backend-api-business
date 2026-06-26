#include "business_patterns.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ================================================================
 * Circuit Breaker — Nygard (2007), "Release It!"
 *
 * State Transitions:
 *   CLOSED ──[failures ≥ threshold]──► OPEN
 *   OPEN   ──[timeout elapsed]──────► HALF_OPEN
 *   HALF_OPEN ──[success]───────────► CLOSED
 *   HALF_OPEN ──[failure]───────────► OPEN
 *
 * Reference: MIT 6.824 Lecture 8 (Fault Tolerance)
 * ================================================================ */

CircuitBreaker cb_create(const char *name) {
    return cb_create_with_config(name, CB_MAX_FAILURES_DEFAULT,
        CB_TIMEOUT_MS_DEFAULT, CB_HALF_OPEN_MAX_DEFAULT);
}

CircuitBreaker cb_create_with_config(const char *name,
    int failure_threshold, uint64_t timeout_ms, int half_open_max) {
    CircuitBreaker cb;
    memset(&cb, 0, sizeof(cb));
    cb.state = CB_STATE_CLOSED;
    cb.failure_threshold = failure_threshold > 0 ? failure_threshold
        : CB_MAX_FAILURES_DEFAULT;
    cb.timeout_ms = timeout_ms > 0 ? timeout_ms : CB_TIMEOUT_MS_DEFAULT;
    cb.half_open_max_requests = half_open_max > 0 ? half_open_max
        : CB_HALF_OPEN_MAX_DEFAULT;
    if (name) {
        strncpy(cb.name, name, 127);
        cb.name[127] = '\0';
    }
    return cb;
}

void cb_reset(CircuitBreaker *cb) {
    cb->state = CB_STATE_CLOSED;
    cb->failure_count = 0;
    cb->half_open_success_count = 0;
    cb->half_open_request_count = 0;
}

static uint64_t now_ms(void) {
    return (uint64_t)((double)clock() / CLOCKS_PER_SEC * 1000.0);
}

bool cb_allow_request(CircuitBreaker *cb) {
    uint64_t now = now_ms();

    if (cb->state == CB_STATE_CLOSED) {
        return true;
    }

    if (cb->state == CB_STATE_OPEN) {
        if (now - cb->last_state_change_ms >= cb->timeout_ms) {
            cb->state = CB_STATE_HALF_OPEN;
            cb->half_open_request_count = 0;
            cb->half_open_success_count = 0;
            cb->last_state_change_ms = now;
            return true;
        }
        cb->total_rejected++;
        return false;
    }

    if (cb->state == CB_STATE_HALF_OPEN) {
        if (cb->half_open_request_count < cb->half_open_max_requests) {
            cb->half_open_request_count++;
            return true;
        }
        cb->total_rejected++;
        return false;
    }

    return false;
}

void cb_record_success(CircuitBreaker *cb) {
    uint64_t now = now_ms();
    cb->total_successes++;

    if (cb->state == CB_STATE_HALF_OPEN) {
        cb->half_open_success_count++;
        if (cb->half_open_success_count >= cb->half_open_max_requests) {
            cb->state = CB_STATE_CLOSED;
            cb->failure_count = 0;
            cb->last_state_change_ms = now;
        }
    }

    if (cb->state == CB_STATE_CLOSED) {
        cb->failure_count = 0;
    }
}

void cb_record_failure(CircuitBreaker *cb) {
    uint64_t now = now_ms();
    cb->total_failures++;
    cb->last_failure_time_ms = now;

    if (cb->state == CB_STATE_CLOSED) {
        cb->failure_count++;
        if (cb->failure_count >= cb->failure_threshold) {
            cb->state = CB_STATE_OPEN;
            cb->last_state_change_ms = now;
        }
    } else if (cb->state == CB_STATE_HALF_OPEN) {
        cb->state = CB_STATE_OPEN;
        cb->last_state_change_ms = now;
    }
}

const char *cb_state_string(CircuitBreakerState state) {
    switch (state) {
        case CB_STATE_CLOSED:    return "CLOSED";
        case CB_STATE_OPEN:      return "OPEN";
        case CB_STATE_HALF_OPEN: return "HALF_OPEN";
        default:                 return "UNKNOWN";
    }
}

void cb_get_stats(const CircuitBreaker *cb, uint64_t *successes,
    uint64_t *failures, uint64_t *rejected, const char **state_name) {
    if (successes)  *successes  = cb->total_successes;
    if (failures)   *failures   = cb->total_failures;
    if (rejected)   *rejected   = cb->total_rejected;
    if (state_name) *state_name = cb_state_string(cb->state);
}

/* ================================================================
 * Idempotency Key Store — Exactly-Once Semantics
 *
 * Check-then-act with TTL-based eviction.
 * Linearizable within single process (no concurrency concerns in C99).
 *
 * Reference: Brandenburger et al. (2015), "Don't settle for eventual:
 * Scalable causal consistency for wide-area storage"
 * ================================================================ */

IdempotencyKeyStore *ik_store_create(uint64_t default_ttl_ms) {
    IdempotencyKeyStore *store = (IdempotencyKeyStore *)malloc(
        sizeof(IdempotencyKeyStore));
    memset(store, 0, sizeof(IdempotencyKeyStore));
    store->default_ttl_ms = default_ttl_ms > 0 ? default_ttl_ms : 3600000;
    return store;
}

void ik_store_destroy(IdempotencyKeyStore *store) {
    free(store);
}

static int ik_store_find_index(IdempotencyKeyStore *store, const char *key) {
    for (int i = 0; i < store->count; i++) {
        if (strncmp(store->entries[i].key, key, IK_KEY_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

IdempotencyKeyStatus ik_store_check(IdempotencyKeyStore *store,
    const char *key) {
    int idx = ik_store_find_index(store, key);
    if (idx < 0) {
        store->misses++;
        return IK_STATUS_NOT_FOUND;
    }
    store->hits++;
    IdempotencyKeyEntry *entry = &store->entries[idx];
    uint64_t now = now_ms();
    if (now - entry->created_at_ms > entry->ttl_ms) {
        store->expired_count++;
        memmove(&store->entries[idx], &store->entries[idx + 1],
            (store->count - idx - 1) * sizeof(IdempotencyKeyEntry));
        store->count--;
        return IK_STATUS_EXPIRED;
    }
    return entry->status;
}

int ik_store_mark_processing(IdempotencyKeyStore *store, const char *key) {
    if (store->count >= IK_MAX_KEYS) return -1;
    int idx = ik_store_find_index(store, key);
    if (idx >= 0) return -2;
    IdempotencyKeyEntry *entry = &store->entries[store->count];
    memset(entry, 0, sizeof(IdempotencyKeyEntry));
    strncpy(entry->key, key, IK_KEY_LEN - 1);
    entry->key[IK_KEY_LEN - 1] = '\0';
    entry->status = IK_STATUS_PROCESSING;
    entry->created_at_ms = now_ms();
    entry->ttl_ms = store->default_ttl_ms;
    store->count++;
    return 0;
}

int ik_store_mark_completed(IdempotencyKeyStore *store, const char *key,
    const char *response, size_t response_size) {
    int idx = ik_store_find_index(store, key);
    if (idx < 0) return -1;
    IdempotencyKeyEntry *entry = &store->entries[idx];
    entry->status = IK_STATUS_COMPLETED;
    entry->response_size = response_size < IK_RESPONSE_LEN - 1
        ? response_size : IK_RESPONSE_LEN - 1;
    memcpy(entry->response, response, entry->response_size);
    entry->response[entry->response_size] = '\0';
    return 0;
}

const char *ik_store_get_response(IdempotencyKeyStore *store,
    const char *key, size_t *out_size) {
    int idx = ik_store_find_index(store, key);
    if (idx < 0) { if (out_size) *out_size = 0; return NULL; }
    if (out_size) *out_size = store->entries[idx].response_size;
    return store->entries[idx].response;
}

int ik_store_evict_expired(IdempotencyKeyStore *store, uint64_t now_ms) {
    int evicted = 0;
    for (int i = store->count - 1; i >= 0; i--) {
        if (now_ms - store->entries[i].created_at_ms
            > store->entries[i].ttl_ms) {
            store->expired_count++;
            int remain = store->count - i - 1;
            if (remain > 0) {
                memmove(&store->entries[i], &store->entries[i + 1],
                    remain * sizeof(IdempotencyKeyEntry));
            }
            store->count--;
            evicted++;
        }
    }
    return evicted;
}

void ik_store_get_stats(const IdempotencyKeyStore *store,
    uint64_t *hits, uint64_t *misses, uint64_t *expired, int *active) {
    if (hits)    *hits    = store->hits;
    if (misses)  *misses  = store->misses;
    if (expired) *expired = store->expired_count;
    if (active)  *active  = store->count;
}

/* ================================================================
 * Token Bucket Rate Limiter — Turner (1986)
 *
 * Algorithm:
 *   now = current_time_ms()
 *   elapsed = now - last_refill_ms
 *   tokens = min(burst, tokens + rate * elapsed / 1000.0)
 *   last_refill_ms = now
 *   if tokens >= requested: tokens -= requested; return true
 *   else: return false
 *
 * Reference: CMU 15-441 Lecture 12 (Traffic Shaping)
 * ================================================================ */

TokenBucket tb_create(const char *name, double rate, double burst) {
    TokenBucket tb;
    memset(&tb, 0, sizeof(tb));
    if (name) {
        strncpy(tb.name, name, 63);
        tb.name[63] = '\0';
    }
    tb.rate = rate > 0.0 ? rate : 1.0;
    tb.burst = burst > 0.0 ? burst : tb.rate;
    tb.tokens = tb.burst;
    tb.last_refill_ms = now_ms();
    return tb;
}

void tb_reset(TokenBucket *tb) {
    tb->tokens = tb->burst;
    tb->last_refill_ms = now_ms();
    tb->total_allowed = 0;
    tb->total_rejected = 0;
}

void tb_refill(TokenBucket *tb, uint64_t now) {
    uint64_t elapsed = now - tb->last_refill_ms;
    if (elapsed == 0) return;
    double new_tokens = tb->rate * ((double)elapsed / 1000.0);
    tb->tokens += new_tokens;
    if (tb->tokens > tb->burst) tb->tokens = tb->burst;
    tb->last_refill_ms = now;
}

bool tb_try_consume(TokenBucket *tb, uint64_t now_ms_val, double tokens) {
    tb_refill(tb, now_ms_val);
    if (tb->tokens >= tokens) {
        tb->tokens -= tokens;
        tb->total_allowed++;
        return true;
    }
    tb->total_rejected++;
    return false;
}

double tb_get_fill_level(const TokenBucket *tb, uint64_t now_ms_val) {
    TokenBucket copy = *tb;
    tb_refill(&copy, now_ms_val);
    return copy.tokens / copy.burst;
}

void tb_get_stats(const TokenBucket *tb, uint64_t *allowed,
    uint64_t *rejected) {
    if (allowed)  *allowed  = tb->total_allowed;
    if (rejected) *rejected = tb->total_rejected;
}

/* ================================================================
 * Exponential Backoff with Jitter
 *
 * Formula: sleep = random(0, min(cap, base * 2^attempt))
 *
 * Full jitter produces uniform distribution of retry times,
 * minimizing correlated retries. Proven superior to:
 *   - Equal jitter: sleep = base/2 + random(0, base/2)
 *   - Decorrelated jitter: sleep = min(cap, random(base, sleep*3))
 *
 * Reference: AWS Architecture Blog, "Exponential Backoff and Jitter"
 *   https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/
 * ================================================================ */

static uint64_t eb_pow2(int n) {
    uint64_t result = 1;
    for (int i = 0; i < n && result < (UINT64_MAX >> 1); i++) {
        result <<= 1;
    }
    return result;
}

static uint64_t eb_random_range(uint64_t max_val) {
    if (max_val == 0) return 0;
    return (uint64_t)(((double)rand() / (double)RAND_MAX) * (double)max_val);
}

ExponentialBackoff eb_create(uint64_t base_ms, uint64_t max_ms,
    int max_retries) {
    ExponentialBackoff eb;
    memset(&eb, 0, sizeof(eb));
    eb.base_delay_ms = base_ms > 0 ? base_ms : 100;
    eb.max_delay_ms = max_ms > 0 ? max_ms : 30000;
    eb.max_retries = max_retries > 0 ? max_retries : 3;
    eb.attempt = 0;
    eb.jitter_factor = 1.0;  /* Full jitter by default */
    return eb;
}

void eb_reset(ExponentialBackoff *eb) {
    eb->attempt = 0;
}

uint64_t eb_next_delay_ms(ExponentialBackoff *eb) {
    if (eb->attempt >= eb->max_retries) return 0;

    uint64_t base = eb->base_delay_ms * eb_pow2(eb->attempt);
    uint64_t capped = base < eb->max_delay_ms ? base : eb->max_delay_ms;
    uint64_t jittered = eb_random_range(capped);

    eb->attempt++;
    eb->total_retries++;
    return jittered;
}

bool eb_should_retry(const ExponentialBackoff *eb) {
    return eb->attempt < eb->max_retries;
}

void eb_record_success(ExponentialBackoff *eb) {
    eb->total_successes++;
    eb_reset(eb);
}

void eb_record_gave_up(ExponentialBackoff *eb) {
    eb->total_gave_up++;
    eb_reset(eb);
}

void eb_get_stats(const ExponentialBackoff *eb, int *attempt,
    uint64_t *total_retries, uint64_t *total_successes,
    uint64_t *total_gave_up) {
    if (attempt)         *attempt         = eb->attempt;
    if (total_retries)   *total_retries   = eb->total_retries;
    if (total_successes) *total_successes = eb->total_successes;
    if (total_gave_up)   *total_gave_up   = eb->total_gave_up;
}

/* ================================================================
 * Bulkhead Pattern — Nygard (2007)
 *
 * Semaphore-based concurrency limiting with metrics.
 * Saturation ratio = current / max_concurrent.
 * If ratio > 0.8, consider scaling or adding bulkheads.
 *
 * Reference: Nygard, "Release It!" Chapter 5 (Stability Patterns)
 * ================================================================ */

Bulkhead bh_create(const char *name, int max_concurrent) {
    Bulkhead bh;
    memset(&bh, 0, sizeof(bh));
    if (name) {
        strncpy(bh.name, name, 63);
        bh.name[63] = '\0';
    }
    bh.max_concurrent = max_concurrent > 0 ? max_concurrent : 10;
    bh.peak_concurrency = 0;
    return bh;
}

int bh_try_acquire(Bulkhead *bh) {
    if (bh->current_count >= bh->max_concurrent) {
        bh->total_rejected++;
        return -1;
    }
    bh->current_count++;
    bh->total_admitted++;
    if (bh->current_count > bh->peak_concurrency) {
        bh->peak_concurrency = bh->current_count;
    }
    return 0;
}

void bh_release(Bulkhead *bh) {
    if (bh->current_count > 0) {
        bh->current_count--;
        bh->total_completed++;
    }
}

void bh_release_with_result(Bulkhead *bh, bool success) {
    if (bh->current_count > 0) {
        bh->current_count--;
    }
    if (success) {
        bh->total_completed++;
    } else {
        bh->total_failed++;
    }
}

bool bh_is_saturated(const Bulkhead *bh) {
    return bh->current_count >= bh->max_concurrent;
}

double bh_saturation_ratio(const Bulkhead *bh) {
    return (double)bh->current_count / (double)bh->max_concurrent;
}

void bh_get_stats(const Bulkhead *bh, int *current, int *peak,
    uint64_t *admitted, uint64_t *rejected, uint64_t *completed,
    uint64_t *failed) {
    if (current)   *current   = bh->current_count;
    if (peak)      *peak      = bh->peak_concurrency;
    if (admitted)  *admitted  = bh->total_admitted;
    if (rejected)  *rejected  = bh->total_rejected;
    if (completed) *completed = bh->total_completed;
    if (failed)    *failed    = bh->total_failed;
}

void bh_reset_stats(Bulkhead *bh) {
    bh->total_admitted  = 0;
    bh->total_rejected  = 0;
    bh->total_completed = 0;
    bh->total_failed    = 0;
    bh->peak_concurrency = bh->current_count;
}

/* ================================================================
 * Composite Resilience Policy
 *
 * Execution pipeline:
 *   1. Check bulkhead → fail fast if saturated
 *   2. Check circuit breaker → fail fast if OPEN
 *   3. Check rate limiter → fail fast if no tokens
 *   4. Execute with retry + backoff
 *   5. Record results
 *
 * Reference: Netflix Hystrix, resilience4j
 * ================================================================ */

ResiliencePolicy resilience_policy_create(CircuitBreaker *cb,
    Bulkhead *bh, ExponentialBackoff *eb, TokenBucket *tb,
    uint64_t timeout_ms) {
    ResiliencePolicy policy;
    memset(&policy, 0, sizeof(policy));
    policy.circuit_breaker = cb;
    policy.bulkhead = bh;
    policy.backoff = eb;
    policy.rate_limiter = tb;
    policy.timeout_ms = timeout_ms > 0 ? timeout_ms : 5000;
    return policy;
}

ResilienceResult resilience_execute(ResiliencePolicy *policy,
    uint64_t now_ms_val, resilience_call_fn fn, void *ctx) {
    /* Step 1: Check bulkhead */
    if (policy->bulkhead && bh_try_acquire(policy->bulkhead) != 0) {
        return RESILIENCE_REJECTED_BULKHEAD;
    }

    /* Step 2: Check circuit breaker */
    if (policy->circuit_breaker
        && !cb_allow_request(policy->circuit_breaker)) {
        if (policy->bulkhead) bh_release(policy->bulkhead);
        return RESILIENCE_REJECTED_CIRCUIT_OPEN;
    }

    /* Step 3: Check rate limiter */
    if (policy->rate_limiter
        && !tb_try_consume(policy->rate_limiter, now_ms_val, 1.0)) {
        if (policy->circuit_breaker)
            cb_record_success(policy->circuit_breaker);
        if (policy->bulkhead) bh_release(policy->bulkhead);
        return RESILIENCE_REJECTED_RATE_LIMITED;
    }

    /* Step 4: Execute with retry */
    if (policy->backoff) eb_reset(policy->backoff);

    int call_result = -1;
    do {
        uint64_t start = now_ms();
        call_result = fn(ctx);
        uint64_t elapsed = now_ms() - start;

        if (call_result == 0) {
            if (policy->circuit_breaker)
                cb_record_success(policy->circuit_breaker);
            if (policy->backoff)
                eb_record_success(policy->backoff);
            if (policy->bulkhead)
                bh_release_with_result(policy->bulkhead, true);
            return RESILIENCE_OK;
        }

        if (elapsed > policy->timeout_ms && call_result != 0) {
            if (policy->circuit_breaker)
                cb_record_failure(policy->circuit_breaker);
            if (policy->backoff)
                eb_record_gave_up(policy->backoff);
            if (policy->bulkhead)
                bh_release_with_result(policy->bulkhead, false);
            return RESILIENCE_TIMEOUT;
        }

        if (policy->backoff && eb_should_retry(policy->backoff)) {
            eb_next_delay_ms(policy->backoff);
            continue;
        }
        break;
    } while (policy->backoff && eb_should_retry(policy->backoff));

    /* Exhausted retries or no backoff configured */
    if (policy->circuit_breaker)
        cb_record_failure(policy->circuit_breaker);
    if (policy->backoff)
        eb_record_gave_up(policy->backoff);
    if (policy->bulkhead)
        bh_release_with_result(policy->bulkhead, false);

    return (policy->backoff
        && policy->backoff->attempt >= policy->backoff->max_retries)
        ? RESILIENCE_RETRIES_EXHAUSTED : RESILIENCE_CALL_FAILED;
}

const char *resilience_result_string(ResilienceResult result) {
    switch (result) {
        case RESILIENCE_OK:                    return "OK";
        case RESILIENCE_REJECTED_BULKHEAD:     return "REJECTED_BULKHEAD";
        case RESILIENCE_REJECTED_CIRCUIT_OPEN: return "REJECTED_CIRCUIT_OPEN";
        case RESILIENCE_REJECTED_RATE_LIMITED: return "REJECTED_RATE_LIMITED";
        case RESILIENCE_TIMEOUT:               return "TIMEOUT";
        case RESILIENCE_CALL_FAILED:           return "CALL_FAILED";
        case RESILIENCE_RETRIES_EXHAUSTED:     return "RETRIES_EXHAUSTED";
        default:                               return "UNKNOWN";
    }
}

/* ================================================================
 * Health Checker — Phi-Accrual Failure Detector
 *
 * Phi-accrual (Hayashibara et al., 2004):
 *   Φ(t_now) = -log10(P(heartbeat_arrives_before_t_now))
 *
 * Uses exponential moving average (EMA) for dynamic adaptation:
 *   mean = α * sample + (1-α) * mean
 *   variance = α * (sample - mean)^2 + (1-α) * variance
 *
 * Φ > threshold ⇒ suspect node failure.
 *
 * Used in: Akka (akka.remote.PhiAccrualFailureDetector),
 *          Apache Cassandra (FailureDetector),
 *          Kubernetes node-lifecycle-controller
 * ================================================================ */

HealthChecker *hc_create(uint64_t default_timeout_ms,
    double phi_threshold, double ema_alpha) {
    HealthChecker *hc = (HealthChecker *)malloc(sizeof(HealthChecker));
    memset(hc, 0, sizeof(HealthChecker));
    hc->default_timeout_ms = default_timeout_ms > 0
        ? default_timeout_ms : 5000;
    hc->default_phi_threshold = phi_threshold > 0.0
        ? phi_threshold : 8.0;
    hc->ema_alpha = (ema_alpha > 0.0 && ema_alpha <= 1.0)
        ? ema_alpha : 0.3;
    return hc;
}

void hc_destroy(HealthChecker *hc) {
    free(hc);
}

int hc_register_target(HealthChecker *hc, const char *target_id) {
    if (hc->count >= HC_MAX_TARGETS) return -1;
    HealthCheckTarget *t = &hc->targets[hc->count];
    memset(t, 0, sizeof(HealthCheckTarget));
    strncpy(t->target_id, target_id, 63);
    t->target_id[63] = '\0';
    t->mean_ms = (double)hc->default_timeout_ms;
    t->variance_ms = 1000.0;
    t->phi_threshold = hc->default_phi_threshold;
    t->is_available = true;
    hc->count++;
    return 0;
}

void hc_record_heartbeat(HealthChecker *hc, const char *target_id,
    uint64_t now_ms_val) {
    for (int i = 0; i < hc->count; i++) {
        HealthCheckTarget *t = &hc->targets[i];
        if (strcmp(t->target_id, target_id) != 0) continue;

        if (t->last_heartbeat_ms > 0) {
            double interval = (double)(now_ms_val
                - t->last_heartbeat_ms);
            double delta = interval - t->mean_ms;
            t->mean_ms = hc->ema_alpha * interval
                + (1.0 - hc->ema_alpha) * t->mean_ms;
            t->variance_ms = hc->ema_alpha * (delta * delta)
                + (1.0 - hc->ema_alpha) * t->variance_ms;
        }

        t->last_heartbeat_ms = now_ms_val;
        t->last_check_ms = now_ms_val;
        t->missed_heartbeats = 0;
        t->is_suspicious = false;
        t->is_available = true;
        return;
    }
}

double hc_compute_phi(HealthChecker *hc, const char *target_id,
    uint64_t now_ms_val) {
    for (int i = 0; i < hc->count; i++) {
        HealthCheckTarget *t = &hc->targets[i];
        if (strcmp(t->target_id, target_id) != 0) continue;

        if (t->last_heartbeat_ms == 0) return 0.0;

        double elapsed = (double)(now_ms_val - t->last_heartbeat_ms);
        double stddev = sqrt(t->variance_ms);
        if (stddev < 1.0) stddev = 1.0;

        double diff = elapsed - t->mean_ms;
        double phi = -log10(exp(-(diff * diff)
            / (2.0 * stddev * stddev)));
        return phi < 0.0 ? 0.0 : phi;
    }
    return 0.0;
}

bool hc_is_available(HealthChecker *hc, const char *target_id,
    uint64_t now_ms_val) {
    for (int i = 0; i < hc->count; i++) {
        HealthCheckTarget *t = &hc->targets[i];
        if (strcmp(t->target_id, target_id) != 0) continue;

        t->last_check_ms = now_ms_val;
        double phi = hc_compute_phi(hc, target_id, now_ms_val);

        if (phi > t->phi_threshold) {
            t->missed_heartbeats++;
            t->is_suspicious = true;
            if (t->missed_heartbeats > 3) {
                t->is_available = false;
                t->total_failures++;
            }
        } else {
            t->missed_heartbeats = 0;
            t->is_suspicious = false;
            t->is_available = true;
        }
        t->total_checks++;
        return t->is_available;
    }
    return false;
}

void hc_check_all(HealthChecker *hc, uint64_t now_ms_val) {
    for (int i = 0; i < hc->count; i++) {
        hc_is_available(hc, hc->targets[i].target_id, now_ms_val);
    }
}

int hc_available_count(const HealthChecker *hc) {
    int count = 0;
    for (int i = 0; i < hc->count; i++) {
        if (hc->targets[i].is_available) count++;
    }
    return count;
}

int hc_suspicious_count(const HealthChecker *hc) {
    int count = 0;
    for (int i = 0; i < hc->count; i++) {
        if (hc->targets[i].is_suspicious) count++;
    }
    return count;
}
