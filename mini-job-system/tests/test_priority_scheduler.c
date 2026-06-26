#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "priority_scheduler.h"

static int tr, tp, tf;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{tp++;printf("PASS\n");}while(0)
#define F(m) do{tf++;printf("FAIL: %s\n",m);return;}while(0)
#define C(c,m) do{if(!(c)){F(m);return;}}while(0)

static void dummy_job(void *ud) { (void)ud; }

static void test_create_mlfq(void)
{
    T("create MLFQ scheduler");
    ps_scheduler_t *ps = ps_create(NULL);
    C(ps != NULL, "create failed");
    C(ps_is_empty(ps), "should be empty");
    C(ps_queue_size(ps) == 0, "queue size 0");
    ps_destroy(ps);
    P();
}

static void test_submit_job(void)
{
    T("submit job");
    ps_scheduler_t *ps = ps_create(NULL);
    int r = ps_submit(ps, 1, dummy_job, NULL, 0, 1);
    C(r == 0, "submit succeeded");
    C(!ps_is_empty(ps), "not empty after submit");
    C(ps_queue_size(ps) == 1, "queue size 1");
    C(ps_total_submitted(ps) == 1, "total submitted 1");
    ps_destroy(ps);
    P();
}

static void test_schedule_next_mlfq(void)
{
    T("schedule next (MLFQ - highest priority first)");
    ps_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.algorithm = PS_SCHED_MLFQ;
    cfg.level_count = 3;
    cfg.levels[0].priority = 0; cfg.levels[0].quantum_ms = 10;
    cfg.levels[1].priority = 1; cfg.levels[1].quantum_ms = 50;
    cfg.levels[2].priority = 2; cfg.levels[2].quantum_ms = 100;

    ps_scheduler_t *ps = ps_create(&cfg);
    ps_submit(ps, 10, dummy_job, NULL, 1, 1); /* medium priority */
    ps_submit(ps, 20, dummy_job, NULL, 0, 1); /* high priority */

    ps_job_entry_t out;
    int ok = ps_schedule_next(ps, &out);
    C(ok, "should dequeue");
    C(out.id == 20, "higher priority job (id=20) should go first");

    ok = ps_schedule_next(ps, &out);
    C(ok, "should dequeue second");
    C(out.id == 10, "then medium priority job (id=10)");
    ps_destroy(ps);
    P();
}

static void test_cancel(void)
{
    T("cancel job");
    ps_scheduler_t *ps = ps_create(NULL);
    ps_submit(ps, 1, dummy_job, NULL, 0, 1);
    int r = ps_cancel(ps, 1);
    C(r == 0, "cancel succeeded");
    C(ps_is_empty(ps), "empty after cancel");
    C(ps_total_completed(ps) == 1, "counted as completed");
    ps_destroy(ps);
    P();
}

static void test_priority_inheritance(void)
{
    T("priority inheritance");
    ps_scheduler_t *ps = ps_create(NULL);
    ps_submit(ps, 1, dummy_job, NULL, 2, 1); /* low priority holder */
    ps_submit(ps, 2, dummy_job, NULL, 0, 1); /* high priority waiter */

    int r = ps_priority_inherit(ps, 1, 2);
    C(r == 1, "priority should be boosted");
    ps_destroy(ps);
    P();
}

static void test_aging(void)
{
    T("aging prevents starvation (API smoke test)");
    ps_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.algorithm = PS_SCHED_MLFQ;
    cfg.level_count = 3;
    cfg.aging_interval_sec = 0; /* age immediately */
    cfg.max_aging_boosts = 3;
    cfg.levels[0].priority = 0; cfg.levels[0].quantum_ms = 10;
    cfg.levels[1].priority = 1; cfg.levels[1].quantum_ms = 50;
    cfg.levels[2].priority = 2; cfg.levels[2].quantum_ms = 100;

    ps_scheduler_t *ps = ps_create(&cfg);
    ps_submit(ps, 1, dummy_job, NULL, 2, 1); /* lowest priority */
    ps_submit(ps, 2, dummy_job, NULL, 0, 1); /* high priority */

    /* Aging should not crash */
    int aged = ps_age_jobs(ps, time(NULL));
    C(aged >= 0, "aging should return non-negative count");

    /* Check queue sizes at different levels */
    int sz0 = ps_queue_size_at(ps, 0);
    int sz2 = ps_queue_size_at(ps, 2);
    C(sz0 >= 0 && sz2 >= 0, "queue sizes valid");
    ps_destroy(ps);
    P();
}

static void test_lottery_select(void)
{
    T("lottery scheduling selects a job");
    ps_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.algorithm = PS_SCHED_LOTTERY;
    cfg.level_count = 1;

    ps_scheduler_t *ps = ps_create(&cfg);
    ps_submit(ps, 1, dummy_job, NULL, 0, 100); /* 100 tickets */
    ps_submit(ps, 2, dummy_job, NULL, 0, 1);   /* 1 ticket */

    unsigned int seed = 42;
    int idx = ps_lottery_select(ps, &seed);
    C(idx >= 0, "should select a job");

    /* Statistically, id=1 should win most of the time, but
     * we just verify selection works */
    C(idx >= 0 && idx < 2, "index in valid range");
    ps_destroy(ps);
    P();
}

int main(void)
{
    tr = tp = tf = 0;
    printf("=== Priority Scheduler Tests ===\n\n");
    test_create_mlfq();
    test_submit_job();
    test_schedule_next_mlfq();
    test_cancel();
    test_priority_inheritance();
    test_aging();
    test_lottery_select();
    printf("\n=== Results: %d run, %d passed, %d failed ===\n", tr, tp, tf);
    return tf > 0 ? 1 : 0;
}
