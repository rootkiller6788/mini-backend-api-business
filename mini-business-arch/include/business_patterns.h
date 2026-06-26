#ifndef BUSINESS_PATTERNS_H
#define BUSINESS_PATTERNS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * business_patterns.h - Enterprise Resilience Patterns (C99)
 *
 * Implements core distributed systems resilience patterns from:
 *   - Michael Nygard, "Release It!" (2007): Circuit Breaker, Bulkhead
 *   - AWS Architecture Blog: Exponential Backoff with Jitter
 *   - Tanenbaum & Van Steen, "Distributed Systems": Idempotency
 *   - Token Bucket: Turner (1986), "New Directions in Communications"
 *
 * Knowledge Coverage:
 *   L4: Idempotency (exactly-once semantics), Bulkhead (fault isolation)
 *   L5: Circuit Breaker (state machine), Token Bucket (rate limiting),
 *       Exponential Backoff + Jitter (retry strategy)
 *   L8: Adaptive concurrency control, failure detection heuristics
 * ================================================================ */

/* -----------------------------------------------------------------
 * SECTION 1: Circuit Breaker (L5: State Machine Pattern)
 *
 * Theorem (Nygard, 2007): A circuit breaker prevents cascading failures
 * by detecting failure thresholds and failing fast. States follow a
 * Markov chain: CLOSED → [failures ≥ threshold] → OPEN → [timeout] →
 * HALF_OPEN → [success] → CLOSED | [failure] → OPEN.
 *
 * Course: MIT 6.824 (Fault Tolerance), CMU 15-440 (Distributed Systems)
 * ----------------------------------------------------------------- */

#define CB_MAX_FAILURES_DEFAULT   5
#define CB_TIMEOUT_MS_DEFAULT  30000
#define CB_HALF_OPEN_MAX_DEFAULT  3

typedef enum {
    CB_STATE_CLOSED = 0,    /* Normal operation, counting failures     */
    CB_STATE_OPEN,           /* Failing fast, waiting for timeout       */
    CB_STATE_HALF_OPEN       /* Testing if downstream has recovered     */
} CircuitBreakerState;

typedef struct {
    CircuitBreakerState state;
    int     failure_count;
    int     failure_threshold;
    int     half_open_max_requests;
    int     half_open_success_count;
    int     half_open_request_count;
    uint64_t last_failure_time_ms;
    uint64_t timeout_ms;
    uint64_t last_state_change_ms;
    uint64_t total_successes;
    uint64_t total_failures;
    uint64_t total_rejected;
    char    name[128];
} CircuitBreaker;

CircuitBreaker cb_create(const char *name);
CircuitBreaker cb_create_with_config(const char *name,
    int failure_threshold, uint64_t timeout_ms, int half_open_max);
void         cb_reset(CircuitBreaker *cb);
bool         cb_allow_request(CircuitBreaker *cb);
void         cb_record_success(CircuitBreaker *cb);
void         cb_record_failure(CircuitBreaker *cb);
const char  *cb_state_string(CircuitBreakerState state);
void         cb_get_stats(const CircuitBreaker *cb, uint64_t *successes,
    uint64_t *failures, uint64_t *rejected, const char **state_name);

/* -----------------------------------------------------------------
 * SECTION 2: Idempotency Key Store (L4: Exactly-Once Semantics)
 *
 * Theorem: An idempotency key guarantees that a request is processed
 * at most once. The check-and-set operation must be atomic (linearizable).
 * For distributed systems, this requires consensus (e.g., Raft/Paxos)
 * or a strongly-consistent store.
 *
 * Pattern: store(key) → if exists return cached result, else process
 * and store. Requires TTL-based eviction to bound memory.
 *
 * Course: MIT 6.824 (Linearizability), Stanford CS 244B (Distributed Systems)
 * ----------------------------------------------------------------- */

#define IK_KEY_LEN      128
#define IK_RESPONSE_LEN 2048
#define IK_MAX_KEYS     256

typedef enum {
    IK_STATUS_NOT_FOUND = 0,  /* Key not yet used                    */
    IK_STATUS_PROCESSING,     /* Request in-flight                   */
    IK_STATUS_COMPLETED,      /* Already processed, cached result    */
    IK_STATUS_EXPIRED          /* TTL expired, key can be reused      */
} IdempotencyKeyStatus;

typedef struct {
    char                 key[IK_KEY_LEN];
    char                 response[IK_RESPONSE_LEN];
    size_t               response_size;
    IdempotencyKeyStatus status;
    uint64_t             created_at_ms;
    uint64_t             ttl_ms;
} IdempotencyKeyEntry;

typedef struct {
    IdempotencyKeyEntry entries[IK_MAX_KEYS];
    int                 count;
    uint64_t            default_ttl_ms;
    uint64_t            hits;
    uint64_t            misses;
    uint64_t            expired_count;
} IdempotencyKeyStore;

IdempotencyKeyStore  *ik_store_create(uint64_t default_ttl_ms);
void                  ik_store_destroy(IdempotencyKeyStore *store);
IdempotencyKeyStatus  ik_store_check(IdempotencyKeyStore *store,
    const char *key);
int                   ik_store_mark_processing(IdempotencyKeyStore *store,
    const char *key);
int                   ik_store_mark_completed(IdempotencyKeyStore *store,
    const char *key, const char *response, size_t response_size);
const char           *ik_store_get_response(IdempotencyKeyStore *store,
    const char *key, size_t *out_size);
int                   ik_store_evict_expired(IdempotencyKeyStore *store,
    uint64_t now_ms);
void                  ik_store_get_stats(const IdempotencyKeyStore *store,
    uint64_t *hits, uint64_t *misses, uint64_t *expired, int *active);

/* -----------------------------------------------------------------
 * SECTION 3: Token Bucket Rate Limiter (L5: Traffic Shaping)
 *
 * Algorithm (Turner, 1986): A token bucket enforces a long-term
 * average rate while allowing short-term bursts. Tokens accumulate
 * at a fixed rate r (tokens/second) up to a maximum burst size b.
 * Each request consumes one token. If insufficient tokens, reject.
 *
 * Properties:
 *   - Long-term rate ≤ r
 *   - Maximum burst = b requests
 *   - Time complexity: O(1) per check
 *
 * Course: CMU 15-441 (Computer Networks), Stanford CS 244 (Advanced Topics in Networking)
 * ----------------------------------------------------------------- */

#define RL_MAX_BUCKETS 16

typedef struct {
    char     name[64];
    double   rate;              /* tokens per second                 */
    double   burst;             /* maximum bucket capacity           */
    double   tokens;            /* current token count               */
    uint64_t last_refill_ms;    /* last token refill timestamp       */
    uint64_t total_allowed;
    uint64_t total_rejected;
} TokenBucket;

TokenBucket tb_create(const char *name, double rate, double burst);
void       tb_reset(TokenBucket *tb);
bool       tb_try_consume(TokenBucket *tb, uint64_t now_ms, double tokens);
void       tb_refill(TokenBucket *tb, uint64_t now_ms);
double     tb_get_fill_level(const TokenBucket *tb, uint64_t now_ms);
void       tb_get_stats(const TokenBucket *tb, uint64_t *allowed,
    uint64_t *rejected);

/* -----------------------------------------------------------------
 * SECTION 4: Exponential Backoff with Jitter (L5: Retry Strategy)
 *
 * Algorithm (AWS Architecture Blog, 2015): Exponential backoff with
 * full jitter minimizes correlated retries in distributed systems.
 *
 *   sleep = random(0, min(cap, base * 2^attempt))
 *
 * Without jitter, N clients retrying simultaneously create correlated
 * thundering herds. Full jitter decorrelates retry timings, reducing
 * contention by factor of 1/N in the worst case (proven by simulation).
 *
 * Course: MIT 6.824 (Distributed Systems), CMU 15-440 (Distributed Systems)
 * ----------------------------------------------------------------- */

typedef struct {
    uint64_t base_delay_ms;    /* initial delay                     */
    uint64_t max_delay_ms;     /* cap / maximum delay               */
    int      max_retries;      /* maximum attempts                  */
    int      attempt;          /* current attempt number            */
    double   jitter_factor;    /* 0.0 = no jitter, 1.0 = full jitter */
    uint64_t total_retries;
    uint64_t total_successes;
    uint64_t total_gave_up;
} ExponentialBackoff;

ExponentialBackoff eb_create(uint64_t base_ms, uint64_t max_ms, int max_retries);
void     eb_reset(ExponentialBackoff *eb);
uint64_t eb_next_delay_ms(ExponentialBackoff *eb);
bool     eb_should_retry(const ExponentialBackoff *eb);
void     eb_record_success(ExponentialBackoff *eb);
void     eb_record_gave_up(ExponentialBackoff *eb);
void     eb_get_stats(const ExponentialBackoff *eb, int *attempt,
    uint64_t *total_retries, uint64_t *total_successes,
    uint64_t *total_gave_up);

/* -----------------------------------------------------------------
 * SECTION 5: Bulkhead Pattern (L4: Fault Isolation)
 *
 * Theorem (Nygard, 2007): Bulkheads partition resources to prevent
 * a failure in one component from consuming all resources. Inspired
 * by ship hull compartmentalization.
 *
 * Two common implementations:
 *   - Semaphore bulkhead: Limit concurrent calls to a downstream.
 *     When saturated, fail fast rather than queue indefinitely.
 *   - Thread pool bulkhead: Dedicated thread pool per downstream.
 *
 * This implementation uses semaphore-based bulkheads with configurable
 * concurrency limits. Monitor saturation rate for capacity planning.
 *
 * Course: MIT 6.824 (Fault Tolerance), CMU 15-440 (Distributed Systems)
 * ----------------------------------------------------------------- */

#define BH_MAX_BULKHEADS 32

typedef struct {
    char     name[64];
    int      max_concurrent;    /* semaphore capacity                */
    int      current_count;     /* currently in-flight               */
    int      peak_concurrency;  /* historical peak                   */
    uint64_t total_admitted;
    uint64_t total_rejected;
    uint64_t total_completed;
    uint64_t total_failed;
} Bulkhead;

Bulkhead  bh_create(const char *name, int max_concurrent);
int       bh_try_acquire(Bulkhead *bh);
void      bh_release(Bulkhead *bh);
void      bh_release_with_result(Bulkhead *bh, bool success);
bool      bh_is_saturated(const Bulkhead *bh);
double    bh_saturation_ratio(const Bulkhead *bh);
void      bh_get_stats(const Bulkhead *bh, int *current, int *peak,
    uint64_t *admitted, uint64_t *rejected, uint64_t *completed,
    uint64_t *failed);
void      bh_reset_stats(Bulkhead *bh);

/* -----------------------------------------------------------------
 * SECTION 6: Composite Resilience Pattern (L8: Adaptive Control)
 *
 * Combines Circuit Breaker + Bulkhead + Retry for full resilience:
 *   1. Check bulkhead capacity (fail fast if saturated)
 *   2. Check circuit breaker (fail fast if open)
 *   3. Attempt with exponential backoff retry
 *   4. Record result in both circuit breaker and bulkhead
 *
 * This is the standard resilience4j/Hystrix pattern used in Netflix OSS.
 *
 * Course: CMU 15-721 (Advanced Database Systems), Stanford CS 349D (Cloud Computing)
 * ----------------------------------------------------------------- */

typedef struct {
    CircuitBreaker     *circuit_breaker;
    Bulkhead           *bulkhead;
    ExponentialBackoff *backoff;
    TokenBucket        *rate_limiter;
    uint64_t            timeout_ms;
} ResiliencePolicy;

typedef int  (*resilience_call_fn)(void *context);

typedef enum {
    RESILIENCE_OK = 0,
    RESILIENCE_REJECTED_BULKHEAD,
    RESILIENCE_REJECTED_CIRCUIT_OPEN,
    RESILIENCE_REJECTED_RATE_LIMITED,
    RESILIENCE_TIMEOUT,
    RESILIENCE_CALL_FAILED,
    RESILIENCE_RETRIES_EXHAUSTED
} ResilienceResult;

ResiliencePolicy resilience_policy_create(CircuitBreaker *cb,
    Bulkhead *bh, ExponentialBackoff *eb, TokenBucket *tb,
    uint64_t timeout_ms);
ResilienceResult resilience_execute(ResiliencePolicy *policy,
    uint64_t now_ms, resilience_call_fn fn, void *ctx);
const char      *resilience_result_string(ResilienceResult result);

/* -----------------------------------------------------------------
 * SECTION 7: Health Check (L8: Failure Detection)
 *
 * Phi-accrual failure detector (Hayashibara et al., 2004):
 * Dynamically adapts to network conditions by tracking heartbeat
 * inter-arrival times with exponential moving average.
 * Unlike static timeout, Phi-accrual handles variance in latency.
 *
 * Used in: Akka, Cassandra, Kubernetes node controller.
 *
 * Course: MIT 6.824 (Failure Detectors), CMU 15-440 (Distributed Systems)
 * ----------------------------------------------------------------- */

#define HC_MAX_TARGETS 64

typedef struct {
    char     target_id[64];
    double   mean_ms;           /* EMA of heartbeat interval        */
    double   variance_ms;       /* EMA of variance                  */
    double   phi_threshold;     /* suspicion threshold (phi value)  */
    uint64_t last_heartbeat_ms;
    uint64_t last_check_ms;
    bool     is_suspicious;
    bool     is_available;
    int      missed_heartbeats;
    uint64_t total_checks;
    uint64_t total_failures;
} HealthCheckTarget;

typedef struct {
    HealthCheckTarget targets[HC_MAX_TARGETS];
    int               count;
    uint64_t          default_timeout_ms;
    double            default_phi_threshold;
    double            ema_alpha;        /* smoothing factor 0..1     */
} HealthChecker;

HealthChecker  *hc_create(uint64_t default_timeout_ms,
    double phi_threshold, double ema_alpha);
void            hc_destroy(HealthChecker *hc);
int             hc_register_target(HealthChecker *hc,
    const char *target_id);
void            hc_record_heartbeat(HealthChecker *hc,
    const char *target_id, uint64_t now_ms);
bool            hc_is_available(HealthChecker *hc,
    const char *target_id, uint64_t now_ms);
double          hc_compute_phi(HealthChecker *hc,
    const char *target_id, uint64_t now_ms);
void            hc_check_all(HealthChecker *hc, uint64_t now_ms);
int             hc_available_count(const HealthChecker *hc);
int             hc_suspicious_count(const HealthChecker *hc);

#endif /* BUSINESS_PATTERNS_H */
