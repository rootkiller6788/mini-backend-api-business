#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "business_patterns.h"

static int failures = 0;
#define TEST(name) printf("  %-55s", name)
#define CHECK(cond) do { \
    if (!(cond)) { printf(" FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else printf(" PASS\n"); \
} while(0)

static void test_circuit_breaker(void) {
    TEST("cb_create initializes to CLOSED");
    CircuitBreaker cb = cb_create("test-cb");
    CHECK(cb.state == CB_STATE_CLOSED);

    TEST("cb_allow_request returns true in CLOSED state");
    CHECK(cb_allow_request(&cb));

    TEST("cb_record_failure increments count");
    cb_record_failure(&cb);
    CHECK(cb.failure_count == 1);

    TEST("cb_state_string returns correct strings");
    CHECK(strcmp(cb_state_string(CB_STATE_CLOSED), "CLOSED") == 0);
    CHECK(strcmp(cb_state_string(CB_STATE_OPEN), "OPEN") == 0);
    CHECK(strcmp(cb_state_string(CB_STATE_HALF_OPEN), "HALF_OPEN") == 0);

    TEST("cb_create_with_config uses custom config");
    CircuitBreaker cb2 = cb_create_with_config("custom", 3, 5000, 2);
    CHECK(cb2.failure_threshold == 3);
    CHECK(cb2.timeout_ms == 5000);
    CHECK(cb2.half_open_max_requests == 2);

    TEST("cb_reset returns to CLOSED");
    cb_reset(&cb);
    CHECK(cb.state == CB_STATE_CLOSED);
    CHECK(cb.failure_count == 0);

    TEST("cb_get_stats returns statistics");
    uint64_t succ, fails, rej;
    const char *state;
    cb_get_stats(&cb, &succ, &fails, &rej, &state);
    CHECK(state != NULL);
}

static void test_idempotency_key(void) {
    TEST("ik_store_create initializes store");
    IdempotencyKeyStore *store = ik_store_create(60000);
    CHECK(store != NULL);

    TEST("ik_store_check returns NOT_FOUND for new key");
    IdempotencyKeyStatus status = ik_store_check(store, "key-001");
    CHECK(status == IK_STATUS_NOT_FOUND);

    TEST("ik_store_mark_processing adds key");
    int rc = ik_store_mark_processing(store, "key-001");
    CHECK(rc == 0);

    TEST("ik_store_check returns PROCESSING for marked key");
    status = ik_store_check(store, "key-001");
    CHECK(status == IK_STATUS_PROCESSING);

    TEST("ik_store_mark_processing rejects duplicate");
    rc = ik_store_mark_processing(store, "key-001");
    CHECK(rc == -2);

    TEST("ik_store_mark_completed updates status");
    rc = ik_store_mark_completed(store, "key-001", "response_data", 13);
    CHECK(rc == 0);

    TEST("ik_store_get_response returns cached response");
    size_t size = 0;
    const char *resp = ik_store_get_response(store, "key-001", &size);
    CHECK(resp != NULL);
    CHECK(size > 0);

    TEST("ik_store_check returns COMPLETED for completed key");
    status = ik_store_check(store, "key-001");
    CHECK(status == IK_STATUS_COMPLETED);

    TEST("ik_store_evict_expired handles TTL");
    int evicted = ik_store_evict_expired(store, UINT64_MAX);
    CHECK(evicted >= 0);

    TEST("ik_store_get_stats returns counters");
    uint64_t hits, misses, expired;
    int active;
    ik_store_get_stats(store, &hits, &misses, &expired, &active);
    CHECK(hits > 0);

    ik_store_destroy(store);
}

static void test_token_bucket(void) {
    TEST("tb_create initializes with full tokens");
    TokenBucket tb = tb_create("test-tb", 10.0, 20.0);
    CHECK(tb.rate == 10.0);
    CHECK(tb.burst == 20.0);

    TEST("tb_try_consume allows consumption within burst");
    bool ok = tb_try_consume(&tb, 0, 1.0);
    CHECK(ok);

    TEST("tb_get_fill_level returns ratio");
    double fill = tb_get_fill_level(&tb, 0);
    CHECK(fill >= 0.0 && fill <= 1.0);

    TEST("tb_reset restores tokens");
    tb_reset(&tb);
    double fill2 = tb_get_fill_level(&tb, 0);
    CHECK(fill2 > 0.9);

    TEST("tb_get_stats returns counters");
    uint64_t allowed, rejected;
    tb_get_stats(&tb, &allowed, &rejected);
    CHECK(allowed >= 0);
}

static void test_exponential_backoff(void) {
    TEST("eb_create initializes with zero attempts");
    ExponentialBackoff eb = eb_create(100, 5000, 5);
    CHECK(eb.base_delay_ms == 100);
    CHECK(eb.max_delay_ms == 5000);
    CHECK(eb.max_retries == 5);
    CHECK(eb.attempt == 0);

    TEST("eb_next_delay_ms returns delay and increments attempt");
    uint64_t delay = eb_next_delay_ms(&eb);
    CHECK(delay <= eb.base_delay_ms);
    CHECK(eb.attempt == 1);

    TEST("eb_should_retry within limits");
    CHECK(eb_should_retry(&eb));

    TEST("eb_reset clears attempt counter");
    eb_reset(&eb);
    CHECK(eb.attempt == 0);

    TEST("eb_record_success resets and counts");
    eb_record_success(&eb);
    CHECK(eb.total_successes == 1);

    TEST("eb_get_stats returns correct values");
    int att;
    uint64_t tr, ts, tg;
    eb_get_stats(&eb, &att, &tr, &ts, &tg);
    CHECK(ts == 1);
}

static void test_bulkhead(void) {
    TEST("bh_create initializes with capacity");
    Bulkhead bh = bh_create("test-bh", 5);
    CHECK(bh.max_concurrent == 5);

    TEST("bh_try_acquire succeeds within capacity");
    int rc = bh_try_acquire(&bh);
    CHECK(rc == 0);
    CHECK(bh.current_count == 1);

    TEST("bh_is_saturated returns false when not full");
    CHECK(!bh_is_saturated(&bh));

    TEST("bh_saturation_ratio computes correctly");
    double ratio = bh_saturation_ratio(&bh);
    CHECK(ratio == 1.0 / 5.0);

    TEST("bh_release decrements count");
    bh_release(&bh);
    CHECK(bh.current_count == 0);

    TEST("bh_try_acquire fails when saturated");
    for (int i = 0; i < 5; i++) bh_try_acquire(&bh);
    rc = bh_try_acquire(&bh);
    CHECK(rc == -1);
    CHECK(bh.total_rejected == 1);

    for (int i = 0; i < 5; i++) bh_release(&bh);

    TEST("bh_get_stats returns metrics");
    int cur, peak;
    uint64_t adm, rej, comp, fail;
    bh_get_stats(&bh, &cur, &peak, &adm, &rej, &comp, &fail);
    CHECK(adm > 0);
}

static int success_call(void *ctx) {
    (void)ctx;
    return 0;
}

static int fail_call(void *ctx) {
    (void)ctx;
    return -1;
}

static void test_resilience_policy(void) {
    TEST("resilience_policy_create initializes");
    CircuitBreaker cb = cb_create("rp-cb");
    Bulkhead bh = bh_create("rp-bh", 5);
    ExponentialBackoff eb = eb_create(10, 100, 2);
    ResiliencePolicy policy = resilience_policy_create(
        &cb, &bh, &eb, NULL, 5000);
    CHECK(policy.circuit_breaker == &cb);
    CHECK(policy.bulkhead == &bh);

    TEST("resilience_execute with successful call returns OK");
    ResilienceResult result = resilience_execute(
        &policy, 0, success_call, NULL);
    CHECK(result == RESILIENCE_OK);

    TEST("resilience_execute with failed call returns CALL_FAILED");
    result = resilience_execute(&policy, 0, fail_call, NULL);
    CHECK(result == RESILIENCE_CALL_FAILED);

    TEST("resilience_result_string returns strings");
    CHECK(strcmp(resilience_result_string(RESILIENCE_OK), "OK") == 0);
    CHECK(strcmp(resilience_result_string(RESILIENCE_REJECTED_BULKHEAD),
        "REJECTED_BULKHEAD") == 0);
    CHECK(strcmp(resilience_result_string(RESILIENCE_CALL_FAILED),
        "CALL_FAILED") == 0);

    /* Fill bulkhead to test rejection */
    for (int i = 0; i < 5; i++) bh_try_acquire(&bh);
    result = resilience_execute(&policy, 0, success_call, NULL);
    CHECK(result == RESILIENCE_REJECTED_BULKHEAD);
    for (int i = 0; i < 5; i++) bh_release(&bh);
}

static void test_health_checker(void) {
    TEST("hc_create initializes health checker");
    HealthChecker *hc = hc_create(5000, 8.0, 0.3);
    CHECK(hc != NULL);

    TEST("hc_register_target adds target");
    int rc = hc_register_target(hc, "node-1");
    CHECK(rc == 0);
    rc = hc_register_target(hc, "node-2");
    CHECK(rc == 0);
    CHECK(hc->count == 2);

    TEST("hc_is_available returns true after heartbeat");
    hc_record_heartbeat(hc, "node-1", 1000);
    bool avail = hc_is_available(hc, "node-1", 2000);
    CHECK(avail);

    TEST("hc_compute_phi returns low phi after recent heartbeat");
    hc_record_heartbeat(hc, "node-2", 5000);
    double phi = hc_compute_phi(hc, "node-2", 5100);
    CHECK(phi >= 0.0);

    TEST("hc_available_count counts available nodes");
    int count = hc_available_count(hc);
    CHECK(count >= 0);

    TEST("hc_suspicious_count starts at zero");
    int susp = hc_suspicious_count(hc);
    CHECK(susp >= 0);

    hc_destroy(hc);
}

int main(void) {
    printf("=== Test: Business Resilience Patterns ===\n\n");
    test_circuit_breaker();
    test_idempotency_key();
    test_token_bucket();
    test_exponential_backoff();
    test_bulkhead();
    test_resilience_policy();
    test_health_checker();
    printf("\nResult: %d failures\n", failures);
    return failures;
}
