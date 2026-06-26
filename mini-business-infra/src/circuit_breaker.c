#include "circuit_breaker.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- rolling window failure entry --- */
typedef struct {
    int64_t timestamp_ms;
    int     failed; /* 1=failure, 0=success */
} cb_window_entry_t;

struct cb_circuit_breaker {
    cb_config_t         config;
    cb_state_t          state;
    int64_t             consecutive_failures;
    int64_t             consecutive_successes;
    int64_t             last_transition_time;
    cb_stats_t          stats;
    cb_state_change_fn  on_change;
    void               *change_user_data;
    cb_window_entry_t  *window;
    int                 window_pos;
    int                 window_filled;
};

static int64_t cb_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

cb_circuit_breaker_t *cb_create(const cb_config_t *config) {
    if (!config) return NULL;
    cb_circuit_breaker_t *cb = (cb_circuit_breaker_t *)calloc(1, sizeof(*cb));
    if (!cb) return NULL;

    cb->config = *config;

    if (config->failure_threshold <= 0)
        cb->config.failure_threshold = CB_DEFAULT_FAILURES;
    if (config->success_threshold <= 0)
        cb->config.success_threshold = CB_DEFAULT_SUCCESSES;
    if (config->timeout_ms <= 0)
        cb->config.timeout_ms = CB_DEFAULT_TIMEOUT_MS;
    if (config->half_open_max_requests <= 0)
        cb->config.half_open_max_requests = CB_DEFAULT_HALF_MAX;
    if (config->sliding_window_size <= 0)
        cb->config.sliding_window_size = 100;
    if (config->sliding_window_time_ms <= 0)
        cb->config.sliding_window_time_ms = 60000;
    if (config->name[0] == '\0')
        snprintf(cb->config.name, CB_MAX_NAME_LEN, "cb-%p", (void *)cb);

    cb->state = CB_CLOSED;
    cb->last_transition_time = cb_now_ms();

    cb->window = (cb_window_entry_t *)calloc(
        (size_t)cb->config.sliding_window_size, sizeof(cb_window_entry_t));
    if (!cb->window) { free(cb); return NULL; }

    return cb;
}

void cb_destroy(cb_circuit_breaker_t *cb) {
    if (!cb) return;
    free(cb->window);
    free(cb);
}

/* --- rolling window failure rate --- */
static int cb_window_failures(cb_circuit_breaker_t *cb) {
    int64_t now = cb_now_ms();
    int64_t cutoff = now - cb->config.sliding_window_time_ms;
    int failures = 0;
    int count = cb->window_filled ? cb->config.sliding_window_size : cb->window_pos;
    for (int i = 0; i < count; i++) {
        if (cb->window[i].timestamp_ms >= cutoff && cb->window[i].failed)
            failures++;
    }
    return failures;
}

static void cb_window_record(cb_circuit_breaker_t *cb, int failed) {
    cb->window[cb->window_pos].timestamp_ms = cb_now_ms();
    cb->window[cb->window_pos].failed = failed;
    cb->window_pos++;
    if (cb->window_pos >= cb->config.sliding_window_size) {
        cb->window_pos = 0;
        cb->window_filled = 1;
    }
}

/* --- state transitions --- */
static void cb_transition(cb_circuit_breaker_t *cb, cb_state_t new_state) {
    cb_state_t old = cb->state;
    if (old == new_state) return;
    cb->state = new_state;
    int64_t now = cb_now_ms();
    cb->last_transition_time = now;
    cb->stats.last_state_change = now;
    cb->consecutive_failures = 0;
    cb->consecutive_successes = 0;
    if (new_state == CB_OPEN) cb->stats.opened_at = now;
    if (cb->on_change) cb->on_change(old, new_state, &cb->stats, cb->change_user_data);
}

int cb_call(cb_circuit_breaker_t *cb, cb_call_fn fn, void *arg,
            int64_t timeout_ms) {
    if (!cb || !fn) return -EINVAL;

    cb->stats.total_calls++;
    int64_t now = cb_now_ms();

    /* --- OPEN: fail fast --- */
    if (cb->state == CB_OPEN) {
        int64_t elapsed = now - cb->last_transition_time;
        if (elapsed >= cb->config.timeout_ms) {
            cb_transition(cb, CB_HALF_OPEN);
        } else {
            cb->stats.total_fast_fails++;
            return -1; /* circuit open */
        }
    }

    /* --- HALF_OPEN: limited probes --- */
    if (cb->state == CB_HALF_OPEN) {
        if (cb->consecutive_successes + cb->consecutive_failures >=
            cb->config.half_open_max_requests) {
            cb->stats.total_fast_fails++;
            return -1; /* too many probes in half-open */
        }
    }

    /* --- execute --- */
    int64_t start = cb_now_ms();
    int result = fn(arg);
    int64_t elapsed = cb_now_ms() - start;

    int timed_out = (timeout_ms > 0 && elapsed > timeout_ms);
    int failed = (result != 0) || timed_out;

    cb_window_record(cb, failed);

    if (failed) {
        cb->stats.total_failures++;
        cb->stats.last_failure_time = now;
        if (timed_out) cb->stats.total_timeouts++;
        cb->consecutive_failures++;
        cb->consecutive_successes = 0;

        if (cb->state == CB_HALF_OPEN) {
            cb_transition(cb, CB_OPEN); /* any failure re-opens */
        } else if (cb->state == CB_CLOSED) {
            int window_fails = cb_window_failures(cb);
            if (window_fails >= cb->config.failure_threshold) {
                cb_transition(cb, CB_OPEN);
            }
        }
    } else {
        cb->stats.total_successes++;
        cb->stats.last_success_time = now;
        cb->consecutive_successes++;
        cb->consecutive_failures = 0;

        if (cb->state == CB_HALF_OPEN &&
            cb->consecutive_successes >= cb->config.success_threshold) {
            cb_transition(cb, CB_CLOSED);
        }
    }

    return result;
}

cb_state_t cb_get_state(cb_circuit_breaker_t *cb) {
    if (!cb) return CB_OPEN;
    /* check if OPEN can transition to HALF_OPEN */
    if (cb->state == CB_OPEN) {
        int64_t elapsed = cb_now_ms() - cb->last_transition_time;
        if (elapsed >= cb->config.timeout_ms) return CB_HALF_OPEN;
    }
    return cb->state;
}

int cb_get_stats(cb_circuit_breaker_t *cb, cb_stats_t *stats) {
    if (!cb || !stats) return -1;
    *stats = cb->stats;
    return 0;
}

void cb_reset(cb_circuit_breaker_t *cb) {
    if (!cb) return;
    cb->consecutive_failures = 0;
    cb->consecutive_successes = 0;
    memset(&cb->stats, 0, sizeof(cb_stats_t));
    memset(cb->window, 0, (size_t)cb->config.sliding_window_size * sizeof(cb_window_entry_t));
    cb->window_pos = 0;
    cb->window_filled = 0;
    cb_transition(cb, CB_CLOSED);
}

int cb_set_state_change_callback(cb_circuit_breaker_t *cb,
                                  cb_state_change_fn fn, void *user_data) {
    if (!cb) return -1;
    cb->on_change = fn;
    cb->change_user_data = user_data;
    return 0;
}

int cb_half_open_probe(cb_circuit_breaker_t *cb, cb_call_fn fn, void *arg,
                        int64_t timeout_ms) {
    if (!cb || !fn) return -EINVAL;
    if (cb->state != CB_OPEN) return cb_call(cb, fn, arg, timeout_ms);
    /* force transition to half_open for probe */
    int64_t now = cb_now_ms();
    int64_t elapsed = now - cb->last_transition_time;
    if (elapsed < cb->config.timeout_ms) {
        elapsed = cb->config.timeout_ms; /* override: allow probe */
    }
    cb_transition(cb, CB_HALF_OPEN);
    return cb_call(cb, fn, arg, timeout_ms);
}
