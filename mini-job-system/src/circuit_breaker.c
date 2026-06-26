#include "circuit_breaker.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * L3 Engineering Structure: Three-state FSM with sliding window failure counting.
 *
 * State transitions:
 *   CLOSED --(failures >= threshold)--> OPEN
 *   OPEN --(cooldown elapsed)---------> HALF_OPEN
 *   HALF_OPEN --(successes >= threshold)--> CLOSED
 *   HALF_OPEN --(any failure)-----------> OPEN
 *
 * L4 Theorem motivation:
 *   - "Fail fast" reduces mean response time under failure from timeout_ms to ~0
 *   - Recovery time = cooldown_ms + (half-open probe time)
 *   - Throughput during OPEN = 0 (total isolation, 100% availability preservation)
 *
 * Reference: Nygard, M. (2007) "Release It!", Chapter 5
 */

const char *cb_state_name(cb_state_t s)
{
    switch (s) {
        case CB_STATE_CLOSED:    return "CLOSED";
        case CB_STATE_OPEN:      return "OPEN";
        case CB_STATE_HALF_OPEN: return "HALF_OPEN";
        default:                  return "UNKNOWN";
    }
}

circuit_breaker_t *cb_create(const cb_config_t *config, const char *name)
{
    circuit_breaker_t *cb = (circuit_breaker_t *)calloc(1, sizeof(*cb));
    if (!cb) return NULL;

    if (config) {
        cb->config = *config;
    } else {
        /* Sensible defaults */
        cb->config.failure_threshold    = 5;
        cb->config.success_threshold    = 3;
        cb->config.timeout_ms           = 5000;
        cb->config.cooldown_ms          = 30000;
        cb->config.half_open_max_requests = 1;
        cb->config.sliding_window_size  = 60;
    }

    cb->state                = CB_STATE_CLOSED;
    cb->consecutive_failures = 0;
    cb->consecutive_successes = 0;
    cb->total_failures       = 0;
    cb->total_successes      = 0;
    cb->total_rejected       = 0;
    cb->half_open_requests   = 0;
    cb->name                 = name ? name : "unnamed";
    return cb;
}

void cb_destroy(circuit_breaker_t *cb)
{
    if (cb) free(cb);
}

void cb_set_state_callback(circuit_breaker_t *cb, cb_state_callback_t cb_fn,
                            void *userdata)
{
    if (!cb) return;
    cb->state_callback     = cb_fn;
    cb->state_callback_ud  = userdata;
}

static void cb_change_state(circuit_breaker_t *cb, cb_state_t new_state)
{
    if (!cb || cb->state == new_state) return;
    cb_state_t old = cb->state;
    cb->state = new_state;

    if (new_state == CB_STATE_OPEN)
        cb->opened_at = time(NULL);

    if (new_state == CB_STATE_HALF_OPEN)
        cb->half_open_requests = 0;

    if (cb->state_callback)
        cb->state_callback(cb, old, new_state, cb->state_callback_ud);
}

/*
 * Core API: check if a request is allowed through the circuit breaker.
 *
 * Algorithm:
 *   - CLOSED: always allow
 *   - OPEN: check cooldown elapsed → transition to HALF_OPEN; else reject
 *   - HALF_OPEN: allow up to half_open_max_requests concurrent probes
 *
 * L5: This is the central decision logic implementing the three-state FSM.
 * O(1) time complexity.
 */
int cb_allow_request(circuit_breaker_t *cb)
{
    if (!cb) return 0;

    switch (cb->state) {
        case CB_STATE_CLOSED:
            return 1;

        case CB_STATE_OPEN: {
            time_t now = time(NULL);
            double elapsed = difftime(now, cb->opened_at);
            if (elapsed * 1000.0 >= (double)cb->config.cooldown_ms) {
                cb_change_state(cb, CB_STATE_HALF_OPEN);
                cb->half_open_requests = 1;
                return 1;
            }
            cb->total_rejected++;
            return 0;
        }

        case CB_STATE_HALF_OPEN:
            if (cb->half_open_requests < cb->config.half_open_max_requests) {
                cb->half_open_requests++;
                return 1;
            }
            cb->total_rejected++;
            return 0;

        default:
            return 0;
    }
}

void cb_report_success(circuit_breaker_t *cb)
{
    if (!cb) return;
    cb->total_successes++;
    cb->last_success_at = time(NULL);

    switch (cb->state) {
        case CB_STATE_CLOSED:
            cb->consecutive_failures = 0;
            break;

        case CB_STATE_HALF_OPEN:
            cb->consecutive_successes++;
            if (cb->consecutive_successes >= cb->config.success_threshold) {
                cb->consecutive_failures = 0;
                cb->consecutive_successes = 0;
                cb_change_state(cb, CB_STATE_CLOSED);
            }
            cb->half_open_requests--;
            if (cb->half_open_requests < 0)
                cb->half_open_requests = 0;
            break;

        case CB_STATE_OPEN:
            /* Should not normally get here, but handle gracefully */
            break;
    }
}

void cb_report_failure(circuit_breaker_t *cb, cb_failure_type_t type)
{
    (void)type;  /* failure type classification reserved for future use */
    if (!cb) return;
    cb->total_failures++;
    cb->last_failure_at = time(NULL);

    cb_failure_window_record(&cb->failure_window);

    switch (cb->state) {
        case CB_STATE_CLOSED:
            cb->consecutive_failures++;
            if (cb->consecutive_failures >= cb->config.failure_threshold)
                cb_transition_to_open(cb);
            break;

        case CB_STATE_HALF_OPEN:
            cb->consecutive_failures++;
            /* Any failure in half-open → back to OPEN (immediate) */
            cb_transition_to_open(cb);
            break;

        case CB_STATE_OPEN:
            /* Already open; record but don't count toward threshold */
            break;
    }
}

void cb_transition_to_open(circuit_breaker_t *cb)
{
    if (!cb) return;
    cb->consecutive_successes = 0;
    cb->half_open_requests    = 0;
    cb_change_state(cb, CB_STATE_OPEN);
}

void cb_transition_to_half_open(circuit_breaker_t *cb)
{
    if (!cb) return;
    cb->half_open_requests = 0;
    cb_change_state(cb, CB_STATE_HALF_OPEN);
}

void cb_transition_to_closed(circuit_breaker_t *cb)
{
    if (!cb) return;
    cb->consecutive_failures  = 0;
    cb->consecutive_successes = 0;
    cb->half_open_requests    = 0;
    cb_change_state(cb, CB_STATE_CLOSED);
}

cb_state_t cb_get_state(const circuit_breaker_t *cb)
{
    return cb ? cb->state : CB_STATE_OPEN;
}

int cb_is_open(const circuit_breaker_t *cb)
{
    return cb && cb->state == CB_STATE_OPEN;
}

int cb_is_closed(const circuit_breaker_t *cb)
{
    return cb && cb->state == CB_STATE_CLOSED;
}

int cb_is_half_open(const circuit_breaker_t *cb)
{
    return cb && cb->state == CB_STATE_HALF_OPEN;
}

int cb_get_consecutive_failures(const circuit_breaker_t *cb)
{
    return cb ? cb->consecutive_failures : 0;
}

int cb_get_total_failures(const circuit_breaker_t *cb)
{
    return cb ? cb->total_failures : 0;
}

int cb_get_total_successes(const circuit_breaker_t *cb)
{
    return cb ? cb->total_successes : 0;
}

int cb_get_total_rejected(const circuit_breaker_t *cb)
{
    return cb ? cb->total_rejected : 0;
}

double cb_get_failure_rate(const circuit_breaker_t *cb)
{
    if (!cb) return 1.0;
    int total = cb->total_failures + cb->total_successes;
    if (total == 0) return 0.0;
    return (double)cb->total_failures / (double)total;
}

double cb_time_until_retry(const circuit_breaker_t *cb)
{
    if (!cb || cb->state != CB_STATE_OPEN) return 0.0;
    double elapsed_ms = difftime(time(NULL), cb->opened_at) * 1000.0;
    double remaining  = (double)cb->config.cooldown_ms - elapsed_ms;
    return remaining > 0 ? remaining / 1000.0 : 0.0;
}

void cb_reset(circuit_breaker_t *cb)
{
    if (!cb) return;
    cb->state                = CB_STATE_CLOSED;
    cb->consecutive_failures  = 0;
    cb->consecutive_successes = 0;
    cb->total_failures        = 0;
    cb->total_successes       = 0;
    cb->total_rejected        = 0;
    cb->half_open_requests    = 0;
    memset(&cb->failure_window, 0, sizeof(cb->failure_window));
}

/*
 * Sliding window failure tracking
 * Records failure timestamps for rate calculation.
 *
 * L5: Circular buffer with head pointer. When full, overwrites oldest.
 * cb_failure_window_count computes failures within [now - window_sec, now].
 */
void cb_failure_window_record(cb_failure_window_t *fw)
{
    if (!fw) return;
    fw->timestamps[fw->head] = time(NULL);
    fw->head = (fw->head + 1) % 256;
    if (fw->count < 256) fw->count++;
}

int cb_failure_window_count(const cb_failure_window_t *fw, int window_sec)
{
    if (!fw || fw->count == 0) return 0;
    time_t now = time(NULL);
    time_t cutoff = now - (time_t)window_sec;
    int count = 0;
    int i;
    for (i = 0; i < fw->count; i++) {
        int idx = (fw->head - 1 - i + 256) % 256;
        if (fw->timestamps[idx] >= cutoff)
            count++;
        else
            break; /* timestamps are in chronological order */
    }
    return count;
}
