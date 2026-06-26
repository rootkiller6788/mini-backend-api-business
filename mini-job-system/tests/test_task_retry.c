#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "task_retry.h"

static int tr, tp, tf;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{tp++;printf("PASS\n");}while(0)
#define F(m) do{tf++;printf("FAIL: %s\n",m);return;}while(0)
#define C(c,m) do{if(!(c)){F(m);return;}}while(0)

static void dummy_task(void *ud) { (void)ud; }

static void test_calc_delay_no_jitter(void)
{
    T("exponential backoff calculation");
    uint64_t d0 = tr_calc_delay(0, 100, 0, NULL);
    uint64_t d1 = tr_calc_delay(1, 100, 0, NULL);
    uint64_t d2 = tr_calc_delay(2, 100, 0, NULL);
    uint64_t d3 = tr_calc_delay(3, 100, 0, NULL);
    C(d0 == 100, "base=100"); /* 0 retries, base stays */
    C(d1 == 200, "retry1=200"); /* 100 * 2^1 */
    C(d2 == 400, "retry2=400"); /* 100 * 2^2 */
    C(d3 == 800, "retry3=800"); /* 100 * 2^3 */
    P();
}

static void test_calc_delay_max_cap(void)
{
    T("delay capped at TR_MAX_DELAY_MS");
    uint64_t d = tr_calc_delay(30, 100000, 0, NULL);
    C(d <= TR_MAX_DELAY_MS, "capped at max");
    P();
}

static void test_calc_delay_with_jitter(void)
{
    T("jitter produces varied results");
    unsigned int seed = 1234;
    uint64_t d = tr_calc_delay(2, 100, 1, &seed);
    /* jitter should keep delay positive */
    C(d > 0, "positive after jitter");
    C(d < TR_MAX_DELAY_MS + 100, "reasonable range");
    P();
}

static void test_register_and_retry(void)
{
    T("register and report result");
    task_retry_t *tr = task_retry_create(16);
    int r = tr_register(tr, 1, dummy_task, NULL, 3, 100);
    C(r == 0, "register ok");
    C(tr_get_retry_count(tr, 1) == 0, "initial count 0");
    C(!tr_is_exhausted(tr, 1), "not exhausted initially");

    int will_retry = tr_report_result(tr, 1, TR_ERR_TIMEOUT);
    C(will_retry == 1, "should retry on timeout");
    C(!tr_is_exhausted(tr, 1), "not exhausted after 1 retry");
    task_retry_destroy(tr);
    P();
}

static void test_exhausted_after_max(void)
{
    T("exhausted after max retries");
    task_retry_t *tr = task_retry_create(16);
    tr_register(tr, 1, dummy_task, NULL, 2, 100);
    tr_report_result(tr, 1, TR_ERR_TIMEOUT);
    tr_report_result(tr, 1, TR_ERR_TIMEOUT);
    int will_retry = tr_report_result(tr, 1, TR_ERR_TIMEOUT);
    C(will_retry == 0, "should not retry after max");
    C(tr_is_exhausted(tr, 1), "should be exhausted");
    task_retry_destroy(tr);
    P();
}

static void test_no_retry_on_success(void)
{
    T("no retry on success");
    task_retry_t *tr = task_retry_create(16);
    tr_register(tr, 1, dummy_task, NULL, 5, 100);
    int will_retry = tr_report_result(tr, 1, TR_ERR_NONE);
    C(will_retry == 0, "TR_ERR_NONE should not retry");
    task_retry_destroy(tr);
    P();
}

static void test_selective_retry(void)
{
    T("selective retry on error codes");
    task_retry_t *tr = task_retry_create(16);
    tr_register(tr, 1, dummy_task, NULL, 3, 100);
    tr_error_code_t codes[] = {TR_ERR_TIMEOUT};
    tr_set_retry_on(tr, 1, codes, 1);

    int r1 = tr_report_result(tr, 1, TR_ERR_TIMEOUT);
    C(r1 == 1, "should retry on TIMEOUT");
    task_retry_destroy(tr);
    P();
}

int main(void)
{
    tr = tp = tf = 0;
    printf("=== Task Retry Tests ===\n\n");
    test_calc_delay_no_jitter();
    test_calc_delay_max_cap();
    test_calc_delay_with_jitter();
    test_register_and_retry();
    test_exhausted_after_max();
    test_no_retry_on_success();
    test_selective_retry();
    printf("\n=== Results: %d run, %d passed, %d failed ===\n", tr, tp, tf);
    return tf > 0 ? 1 : 0;
}
