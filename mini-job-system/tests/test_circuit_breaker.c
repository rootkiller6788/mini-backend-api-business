#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "circuit_breaker.h"

static int tr, tp, tf;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{tp++;printf("PASS\n");}while(0)
#define F(m) do{tf++;printf("FAIL: %s\n",m);return;}while(0)
#define C(c,m) do{if(!(c)){F(m);return;}}while(0)

static void test_create_closed(void)
{
    T("create circuit breaker (defaults to CLOSED)");
    circuit_breaker_t *cb = cb_create(NULL, "test");
    C(cb != NULL, "create failed");
    C(cb_get_state(cb) == CB_STATE_CLOSED, "should start CLOSED");
    C(cb_is_closed(cb), "is_closed true");
    C(!cb_is_open(cb), "not open");
    C(!cb_is_half_open(cb), "not half_open");
    cb_destroy(cb);
    P();
}

static void test_allow_when_closed(void)
{
    T("allow requests when CLOSED");
    circuit_breaker_t *cb = cb_create(NULL, "test");
    C(cb_allow_request(cb), "should allow request");
    C(cb_allow_request(cb), "should allow second request");
    cb_destroy(cb);
    P();
}

static void test_transition_to_open(void)
{
    T("transition to OPEN after threshold failures");
    cb_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.failure_threshold = 3;
    cfg.success_threshold = 2;
    cfg.cooldown_ms       = 60000;
    circuit_breaker_t *cb = cb_create(&cfg, "test");

    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    C(cb_get_state(cb) == CB_STATE_CLOSED, "still closed after 2 failures");

    cb_report_failure(cb, CB_FAIL_CONNECTION);
    C(cb_get_state(cb) == CB_STATE_OPEN, "should be OPEN after 3rd failure");
    C(cb_is_open(cb), "is_open true");
    cb_destroy(cb);
    P();
}

static void test_reject_when_open(void)
{
    T("reject requests when OPEN");
    circuit_breaker_t *cb = cb_create(NULL, "test");
    cb_transition_to_open(cb);
    C(!cb_allow_request(cb), "should reject when OPEN");
    C(cb_get_total_rejected(cb) == 1, "rejected count incremented");
    cb_destroy(cb);
    P();
}

static void test_success_resets_closed(void)
{
    T("success resets consecutive failures when CLOSED");
    circuit_breaker_t *cb = cb_create(NULL, "test");
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    C(cb_get_consecutive_failures(cb) == 2, "2 consecutive failures");
    cb_report_success(cb);
    C(cb_get_consecutive_failures(cb) == 0, "reset to 0 after success");
    cb_destroy(cb);
    P();
}

static void test_half_open_to_closed(void)
{
    T("half-open to closed after success threshold");
    cb_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.success_threshold = 2;
    cfg.half_open_max_requests = 3;
    cfg.failure_threshold = 2;
    cfg.cooldown_ms = 100; /* short cooldown */
    circuit_breaker_t *cb = cb_create(&cfg, "test");

    /* Force open then wait */
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    C(cb_is_open(cb), "should be OPEN");

    /* Allow it to go to HALF_OPEN */
    cb->opened_at = time(NULL) - 10; /* 10 seconds ago */
    C(cb_allow_request(cb), "should allow half-open probe");
    C(cb_is_half_open(cb), "should be HALF_OPEN");

    cb_report_success(cb);
    cb_report_success(cb);
    C(cb_is_closed(cb), "should return to CLOSED after 2 successes");
    cb_destroy(cb);
    P();
}

static void test_half_open_failure(void)
{
    T("half-open failure returns to OPEN");
    cb_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.failure_threshold = 2;
    cfg.cooldown_ms = 100;
    circuit_breaker_t *cb = cb_create(&cfg, "test");

    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    cb->opened_at = time(NULL) - 10;
    C(cb_allow_request(cb), "half-open probe accepted");
    C(cb_is_half_open(cb), "should be HALF_OPEN");

    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    C(cb_is_open(cb), "should go back to OPEN on failure in half-open");
    cb_destroy(cb);
    P();
}

static void test_failure_rate(void)
{
    T("failure rate computation");
    circuit_breaker_t *cb = cb_create(NULL, "test");
    C(cb_get_failure_rate(cb) == 0.0, "0% failure rate initially");
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    cb_report_success(cb);
    double rate = cb_get_failure_rate(cb);
    C(rate > 0.5, "failure rate should be > 50%");
    cb_destroy(cb);
    P();
}

static void test_reset(void)
{
    T("reset clears all counters");
    circuit_breaker_t *cb = cb_create(NULL, "test");
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    cb_report_failure(cb, CB_FAIL_TIMEOUT);
    cb_reset(cb);
    C(cb_is_closed(cb), "should be CLOSED after reset");
    C(cb_get_consecutive_failures(cb) == 0, "no failures after reset");
    C(cb_get_total_failures(cb) == 0, "total failures 0");
    C(cb_get_total_successes(cb) == 0, "total successes 0");
    C(cb_get_total_rejected(cb) == 0, "total rejected 0");
    cb_destroy(cb);
    P();
}

int main(void)
{
    tr = tp = tf = 0;
    printf("=== Circuit Breaker Tests ===\n\n");
    test_create_closed();
    test_allow_when_closed();
    test_transition_to_open();
    test_reject_when_open();
    test_success_resets_closed();
    test_half_open_to_closed();
    test_half_open_failure();
    test_failure_rate();
    test_reset();
    printf("\n=== Results: %d run, %d passed, %d failed ===\n", tr, tp, tf);
    return tf > 0 ? 1 : 0;
}
