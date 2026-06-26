#ifndef CIRCUIT_BREAKER_H
#define CIRCUIT_BREAKER_H

#include <stdint.h>
#include <time.h>

/*
 * L2 Core Concept: Circuit Breaker pattern (Nygard 2007, "Release It!")
 * Prevents cascading failures by detecting failing downstream services
 * and temporarily blocking requests to allow recovery.
 *
 * L3 Engineering Structure: Three-state FSM
 *   CLOSED  → normal operation, count failures
 *   OPEN    → reject immediately, no calls to downstream
 *   HALF_OPEN → probe with limited requests to test recovery
 *
 * L4 Standards/Theorems:
 *   - Fail-fast principle: rapid failure < slow timeout (Postel's robustness)
 *   - CAP Theorem implication: Circuit breaker provides availability at the
 *     cost of consistency (rejects requests to protect the system).
 *   - Recovery time obeys: T_recovery ≥ MTTR (Mean Time To Repair) of downstream
 *   - Little's Law applied: if failure_rate > recovery_rate, system eventually
 *     saturates → circuit breaker is a form of backpressure.
 *
 * L5 Algorithms:
 *   - Failure counting with sliding window (configurable threshold)
 *   - Half-open probe with success threshold
 *   - Exponential backoff for retry attempts during OPEN
 *
 * L8 Advanced Topics:
 *   - Adaptive thresholds (dynamic tuning based on load)
 *   - Multi-level circuit breakers (per-endpoint + per-service)
 *   - Integration with health-check systems
 *
 * Reference:
 *   - Nygard, M. (2007) "Release It!", Chapter 5: Stability Patterns
 *   - Netflix Hystrix (2011): Circuit breaker in microservices
 *   - Resilience4j: Modern Java circuit breaker
 *   - MIT 6.824: Fault-Tolerant Systems (fail-stop vs fail-recovery)
 */

/* Circuit breaker states */
typedef enum {
    CB_STATE_CLOSED    = 0,
    CB_STATE_OPEN      = 1,
    CB_STATE_HALF_OPEN = 2
} cb_state_t;

/* Failure type classification */
typedef enum {
    CB_FAIL_TIMEOUT     = 0,
    CB_FAIL_CONNECTION  = 1,
    CB_FAIL_HTTP_5XX    = 2,
    CB_FAIL_CUSTOM      = 3
} cb_failure_type_t;

/* Circuit breaker configuration */
typedef struct {
    int     failure_threshold;       /* consecutive failures to open circuit */
    int     success_threshold;       /* successes in half-open to close */
    int     timeout_ms;              /* operation timeout */
    int     cooldown_ms;             /* time to wait before half-open probe */
    int     half_open_max_requests;  /* max requests during half-open */
    int     sliding_window_size;     /* window size for failure counting */
} cb_config_t;

/* Failure counter with sliding window */
typedef struct {
    time_t  timestamps[256];
    int     count;
    int     head;
} cb_failure_window_t;

/* Circuit breaker instance */
typedef struct circuit_breaker_t circuit_breaker_t;

typedef void (*cb_state_callback_t)(circuit_breaker_t *cb, cb_state_t old_state,
                                     cb_state_t new_state, void *userdata);

struct circuit_breaker_t {
    cb_state_t           state;
    cb_config_t          config;
    int                  consecutive_failures;
    int                  consecutive_successes;
    int                  total_failures;
    int                  total_successes;
    int                  total_rejected;
    time_t               opened_at;
    time_t               last_failure_at;
    time_t               last_success_at;
    cb_failure_window_t  failure_window;
    int                  half_open_requests;
    cb_state_callback_t  state_callback;
    void                *state_callback_ud;
    const char          *name;
};

/* Lifecycle */
circuit_breaker_t *cb_create(const cb_config_t *config, const char *name);
void               cb_destroy(circuit_breaker_t *cb);

/* Core API: check if request is allowed */
int  cb_allow_request(circuit_breaker_t *cb);

/* Report outcome */
void cb_report_success(circuit_breaker_t *cb);
void cb_report_failure(circuit_breaker_t *cb, cb_failure_type_t type);

/* State transitions (manual) */
void cb_transition_to_open(circuit_breaker_t *cb);
void cb_transition_to_half_open(circuit_breaker_t *cb);
void cb_transition_to_closed(circuit_breaker_t *cb);

/* State queries */
cb_state_t cb_get_state(const circuit_breaker_t *cb);
int        cb_is_open(const circuit_breaker_t *cb);
int        cb_is_closed(const circuit_breaker_t *cb);
int        cb_is_half_open(const circuit_breaker_t *cb);

/* Statistics */
int    cb_get_consecutive_failures(const circuit_breaker_t *cb);
int    cb_get_total_failures(const circuit_breaker_t *cb);
int    cb_get_total_successes(const circuit_breaker_t *cb);
int    cb_get_total_rejected(const circuit_breaker_t *cb);
double cb_get_failure_rate(const circuit_breaker_t *cb);

/* Time until allows retry (seconds) */
double cb_time_until_retry(const circuit_breaker_t *cb);

/* Set state change callback */
void cb_set_state_callback(circuit_breaker_t *cb, cb_state_callback_t cb_fn,
                            void *userdata);

/* Reset all counters */
void cb_reset(circuit_breaker_t *cb);

/* Utility: state enum to human-readable string */
const char *cb_state_name(cb_state_t state);

/* Sliding window failure tracking */
void cb_failure_window_record(cb_failure_window_t *fw);
int  cb_failure_window_count(const cb_failure_window_t *fw, int window_sec);

#endif
