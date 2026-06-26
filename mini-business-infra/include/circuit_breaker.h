#ifndef CIRCUIT_BREAKER_H
#define CIRCUIT_BREAKER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CB_MAX_NAME_LEN         64
#define CB_DEFAULT_FAILURES     5
#define CB_DEFAULT_TIMEOUT_MS   30000
#define CB_DEFAULT_SUCCESSES    3
#define CB_DEFAULT_HALF_MAX     3

/**
 * Circuit Breaker States (L3: Engineering Structure)
 *
 * Implements the Circuit Breaker pattern from Nygard "Release It!" (2007).
 * State machine ensures fail-fast behavior under degraded conditions.
 *
 * CLOSED → OPEN: failure_threshold exceeded
 * OPEN → HALF_OPEN: after timeout_ms expires
 * HALF_OPEN → CLOSED: success_threshold consecutive successes
 * HALF_OPEN → OPEN: any single failure
 */
typedef enum {
    CB_CLOSED    = 0,
    CB_OPEN      = 1,
    CB_HALF_OPEN = 2
} cb_state_t;

/** Circuit Breaker runtime statistics */
typedef struct {
    int64_t         total_calls;
    int64_t         total_successes;
    int64_t         total_failures;
    int64_t         total_timeouts;
    int64_t         total_fast_fails;
    int64_t         last_failure_time;
    int64_t         last_success_time;
    int64_t         opened_at;
    int64_t         last_state_change;
} cb_stats_t;

/**
 * Circuit Breaker Configuration
 *
 * Theory (L4): The failure_threshold implements a counting-based
 * failure detector. Combined with timeout_ms, this forms a
 * time-bounded failure counting window — a practical approximation
 * of the Phi-accrual failure detector (Hayashibara et al., 2004).
 */
typedef struct {
    char            name[CB_MAX_NAME_LEN];
    int32_t         failure_threshold;     /* consecutive failures to trip */
    int32_t         success_threshold;     /* consecutive successes to close */
    int64_t         timeout_ms;            /* time in OPEN before HALF_OPEN */
    int32_t         half_open_max_requests; /* max probes in HALF_OPEN */
    int32_t         sliding_window_size;    /* window for rolling failure count */
    int64_t         sliding_window_time_ms; /* time span for rolling window */
} cb_config_t;

/** Callback for executing a guarded operation. Returns 0=success, non-zero=failure */
typedef int (*cb_call_fn)(void *arg);

/** Callback invoked on state transitions */
typedef void (*cb_state_change_fn)(cb_state_t from, cb_state_t to,
                                    const cb_stats_t *stats, void *user_data);

typedef struct cb_circuit_breaker cb_circuit_breaker_t;

cb_circuit_breaker_t *cb_create(const cb_config_t *config);
void                  cb_destroy(cb_circuit_breaker_t *cb);

int                   cb_call(cb_circuit_breaker_t *cb, cb_call_fn fn,
                              void *arg, int64_t timeout_ms);
cb_state_t            cb_get_state(cb_circuit_breaker_t *cb);
int                   cb_get_stats(cb_circuit_breaker_t *cb, cb_stats_t *stats);
void                  cb_reset(cb_circuit_breaker_t *cb);
int                   cb_set_state_change_callback(cb_circuit_breaker_t *cb,
                                                    cb_state_change_fn fn,
                                                    void *user_data);
int                   cb_half_open_probe(cb_circuit_breaker_t *cb,
                                          cb_call_fn fn, void *arg,
                                          int64_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
