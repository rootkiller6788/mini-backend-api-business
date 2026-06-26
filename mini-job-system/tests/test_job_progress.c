#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "job_progress.h"

static int tr, tp, tf;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{tp++;printf("PASS\n");}while(0)
#define F(m) do{tf++;printf("FAIL: %s\n",m);return;}while(0)
#define C(c,m) do{if(!(c)){F(m);return;}}while(0)

static void test_create(void)
{
    T("create progress tracker");
    job_progress_t *jp = job_progress_create();
    C(jp != NULL, "create failed");
    C(jp_get_state(jp) == JP_STATE_QUEUED, "initial state QUEUED");
    C(jp_get_progress(jp) == 0.0, "initial progress 0");
    job_progress_destroy(jp);
    P();
}

static void test_state_transition(void)
{
    T("state transitions");
    job_progress_t *jp = job_progress_create();
    jp_set_total_steps(jp, 10);
    int r = jp_transition_state(jp, JP_STATE_RUNNING, "start");
    C(r == 0, "transition to RUNNING");
    C(jp_get_state(jp) == JP_STATE_RUNNING, "state is RUNNING");
    job_progress_destroy(jp);
    P();
}

static void test_progress_update(void)
{
    T("progress update");
    job_progress_t *jp = job_progress_create();
    jp_set_total_steps(jp, 100);
    jp_transition_state(jp, JP_STATE_RUNNING, "start");

    int r = jp_update_progress(jp, 50, "halfway");
    C(r == 0, "update ok");
    C(jp_get_progress(jp) >= 49.9 && jp_get_progress(jp) <= 50.1, "progress ~50%");
    job_progress_destroy(jp);
    P();
}

static void test_increment(void)
{
    T("increment progress");
    job_progress_t *jp = job_progress_create();
    jp_set_total_steps(jp, 5);
    jp_transition_state(jp, JP_STATE_RUNNING, "start");

    jp_increment_progress(jp, "step 1");
    jp_increment_progress(jp, "step 2");
    jp_increment_progress(jp, "step 3");
    C(jp_get_progress(jp) >= 59.9 && jp_get_progress(jp) <= 60.1, "3/5 = 60%");
    job_progress_destroy(jp);
    P();
}

static void test_fail_and_cancel(void)
{
    T("fail and cancel transitions");
    job_progress_t *jp = job_progress_create();
    jp_fail(jp, "error");
    C(jp_get_state(jp) == JP_STATE_FAILED, "state is FAILED");

    job_progress_t *jp2 = job_progress_create();
    jp_cancel(jp2, "user cancelled");
    C(jp_get_state(jp2) == JP_STATE_CANCELLED, "state is CANCELLED");

    job_progress_destroy(jp);
    job_progress_destroy(jp2);
    P();
}

static void test_elapsed_time(void)
{
    T("elapsed time tracking");
    job_progress_t *jp = job_progress_create();
    jp_transition_state(jp, JP_STATE_RUNNING, "start");
    /* After start, elapsed should be near 0 */
    time_t e = jp_get_elapsed(jp);
    C(e >= 0, "elapsed non-negative");
    job_progress_destroy(jp);
    P();
}

static void test_state_name(void)
{
    T("state name utility");
    C(strcmp(jp_state_name(JP_STATE_QUEUED), "QUEUED") == 0, "QUEUED name");
    C(strcmp(jp_state_name(JP_STATE_RUNNING), "RUNNING") == 0, "RUNNING name");
    C(strcmp(jp_state_name(JP_STATE_SUCCEEDED), "SUCCEEDED") == 0, "SUCCEEDED name");
    C(strcmp(jp_state_name(JP_STATE_FAILED), "FAILED") == 0, "FAILED name");
    C(strcmp(jp_state_name(JP_STATE_CANCELLED), "CANCELLED") == 0, "CANCELLED name");
    P();
}

int main(void)
{
    tr = tp = tf = 0;
    printf("=== Job Progress Tests ===\n\n");
    test_create();
    test_state_transition();
    test_progress_update();
    test_increment();
    test_fail_and_cancel();
    test_elapsed_time();
    test_state_name();
    printf("\n=== Results: %d run, %d passed, %d failed ===\n", tr, tp, tf);
    return tf > 0 ? 1 : 0;
}
