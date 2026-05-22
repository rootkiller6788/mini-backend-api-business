#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cron_scheduler.h"
#include "delayed_queue.h"
#include "worker_pool.h"
#include "task_retry.h"
#include "job_progress.h"

/* ====== Utility ====== */
static void ms_sleep(int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static int g_counter = 0;
static int g_errors  = 0;
static int g_done    = 0;

/* ====== Task implementations ====== */
static void pipeline_stage_1(void *userdata)
{
    job_progress_t *jp = (job_progress_t *)userdata;
    jp_transition_state(jp, JP_STATE_RUNNING, "stage-1: fetching data");
    ms_sleep(50);
    jp_increment_progress(jp, "fetched chunk 1");
    ms_sleep(50);
    jp_increment_progress(jp, "fetched chunk 2");
    jp_transition_state(jp, JP_STATE_SUCCEEDED, "stage-1 done");
}

static void pipeline_stage_2(void *userdata)
{
    job_progress_t *jp = (job_progress_t *)userdata;
    jp_transition_state(jp, JP_STATE_RUNNING, "stage-2: processing");
    ms_sleep(60);
    jp_increment_progress(jp, "processed batch A");
    ms_sleep(60);
    jp_increment_progress(jp, "processed batch B");
    ms_sleep(40);
    jp_increment_progress(jp, "processed batch C");
    jp_transition_state(jp, JP_STATE_SUCCEEDED, "stage-2 done");
}

static void pipeline_stage_3(void *userdata)
{
    job_progress_t *jp = (job_progress_t *)userdata;
    jp_transition_state(jp, JP_STATE_RUNNING, "stage-3: saving results");
    ms_sleep(80);
    jp_increment_progress(jp, "saved to DB");
    jp_transition_state(jp, JP_STATE_SUCCEEDED, "stage-3 done");
}

static void mixed_task(void *userdata)
{
    int *flag = (int *)userdata;
    (*flag)++;
}

/* ====== Demo 1: Multi-stage Pipeline ====== */
static void demo_pipeline(void)
{
    printf("\n========== ADVANCED DEMO 1: Multi-stage Pipeline ==========\n\n");

    job_progress_t *stages[3];
    int i;
    for (i = 0; i < 3; i++) {
        stages[i] = job_progress_create();
        stages[i]->job_id = 100 + i;
        jp_set_total_steps(stages[i], (i == 0) ? 2 : (i == 1) ? 3 : 1);
    }

    wp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_workers = 3;
    cfg.queue_size  = 16;
    worker_pool_t *pool = worker_pool_create(&cfg);

    printf("  Stage 1: Fetching data...\n");
    wp_submit(pool, 100, pipeline_stage_1, stages[0], NULL, NULL, "pipeline");
    while (jp_get_state(stages[0]) != JP_STATE_SUCCEEDED) ms_sleep(10);
    printf("  -> Stage 1 complete (%.1f%%)\n", jp_get_progress(stages[0]));

    printf("  Stage 2: Processing data...\n");
    wp_submit(pool, 101, pipeline_stage_2, stages[1], NULL, NULL, "pipeline");
    while (jp_get_state(stages[1]) != JP_STATE_SUCCEEDED) ms_sleep(10);
    printf("  -> Stage 2 complete (%.1f%%)\n", jp_get_progress(stages[1]));

    printf("  Stage 3: Saving results...\n");
    wp_submit(pool, 102, pipeline_stage_3, stages[2], NULL, NULL, "pipeline");
    while (jp_get_state(stages[2]) != JP_STATE_SUCCEEDED) ms_sleep(10);
    printf("  -> Stage 3 complete (%.1f%%)\n", jp_get_progress(stages[2]));

    printf("\n  Pipeline summary:\n");
    for (i = 0; i < 3; i++) {
        printf("    Stage %d: total=%ds elapsed=%llds\n",
               i + 1,
               stages[i]->total_steps,
               (long long)jp_get_elapsed(stages[i]));
    }

    wp_shutdown(pool);
    ms_sleep(100);
    worker_pool_destroy(pool);
    for (i = 0; i < 3; i++) job_progress_destroy(stages[i]);
}

/* ====== Demo 2: Complex Cron Patterns ====== */
static void demo_cron_patterns(void)
{
    printf("\n========== ADVANCED DEMO 2: Complex Cron Patterns ==========\n\n");

    const char *patterns[] = {
        "0 0 9 * 1-5",
        "*/10 */2 * * *",
        "0,15,30,45 0 * * *",
        "0 0 1 1,6 *",
        "30 8-18 * * *",
        "*/5 * * * * *",
        "0 30 14 15 * *",
        NULL
    };

    int pi;
    for (pi = 0; patterns[pi]; pi++) {
        cron_expression_t expr;
        memset(&expr, 0, sizeof(expr));

        if (cron_parse_expression(&expr, patterns[pi]) == 0) {
            time_t next = cron_next_fire_time(&expr, time(NULL));
            struct tm tm_val;
#ifdef _WIN32
            struct tm *ptm = localtime(&next);
            if (ptm) tm_val = *ptm;
#else
            localtime_r(&next, &tm_val);
#endif

            printf("  \"%-20s\" next: %04d-%02d-%02d %02d:%02d:%02d\n",
                   patterns[pi],
                   tm_val.tm_year + 1900, tm_val.tm_mon + 1,
                   tm_val.tm_mday, tm_val.tm_hour,
                   tm_val.tm_min, tm_val.tm_sec);

            int day;
            printf("    Next 5 fire times: ");
            time_t t = next;
            for (day = 0; day < 5; day++) {
                t = cron_next_fire_time(&expr, t);
                if (t == (time_t)-1) { printf("END"); break; }
                struct tm n;
#ifdef _WIN32
                ptm = localtime(&t);
                if (ptm) n = *ptm;
#else
                localtime_r(&t, &n);
#endif
                printf("%s%02d:%02d:%02d",
                       day > 0 ? ", " : "",
                       n.tm_hour, n.tm_min, n.tm_sec);
            }
            printf("\n");
        } else {
            printf("  \"%-20s\" -> PARSE ERROR\n", patterns[pi]);
        }
    }
}

/* ====== Demo 3: Delay Levels Stress Test ====== */
static void test_enqueue_at_all_levels(delayed_queue_t *dq)
{
    dq_enqueue_level(dq, 4001, "key_l1_s",   mixed_task, &g_counter, DQ_LEVEL_1S);
    dq_enqueue_level(dq, 4002, "key_l2_10s", mixed_task, &g_counter, DQ_LEVEL_10S);
    dq_enqueue_level(dq, 4003, "key_l3_1m",  mixed_task, &g_counter, DQ_LEVEL_1MIN);
    dq_enqueue_level(dq, 4004, "key_l4_10m", mixed_task, &g_counter, DQ_LEVEL_10MIN);
    dq_enqueue_level(dq, 4005, "key_l5_1h",  mixed_task, &g_counter, DQ_LEVEL_1HR);

    dq_enqueue(dq, 4006, "custom_500ms", mixed_task, &g_counter, 500);
    dq_enqueue(dq, 4007, "custom_2s",    mixed_task, &g_counter, 2000);
    dq_enqueue(dq, 4008, "custom_3s",    mixed_task, &g_counter, 3000);
}

static void demo_delay_levels(void)
{
    printf("\n========== ADVANCED DEMO 3: Delay Levels + Dedup ==========\n\n");

    delayed_queue_t *dq = delayed_queue_create(32, 1);
    g_counter = 0;

    printf("  Enqueuing 8 jobs at various delay levels...\n");
    test_enqueue_at_all_levels(dq);
    printf("  Queue size: %d\n", dq_size(dq));

    printf("\n  Testing deduplication:\n");
    dq_enqueue_level(dq, 4009, "key_l1_s", mixed_task, &g_counter, DQ_LEVEL_1S);
    printf("  Re-enqueued 'key_l1_s' -> size is still: %d\n", dq_size(dq));

    dq_cancel(dq, 4007);
    printf("  Cancelled job 4007\n");

    dq_entry_t entry;
    if (dq_peek(dq, &entry)) {
        printf("\n  Earliest fire: id=%llu key=%s delay=%llums\n",
               (unsigned long long)entry.id, entry.key,
               (unsigned long long)entry.delay_ms);
    }

    printf("\n  Draining queue (simulating time)...\n");
    time_t fake_now = time(NULL);
    fake_now += 4;

    int dequeued = 0;
    while (dq_dequeue(dq, fake_now, &entry)) {
        printf("  -> Dequeued: id=%llu key=%s\n",
               (unsigned long long)entry.id, entry.key);
        if (entry.callback) entry.callback(entry.userdata);
        dequeued++;
    }
    printf("  Total dequeued: %d, counter: %d\n", dequeued, g_counter);
    printf("  Remaining: %d (waiting for longer delays)\n", dq_size(dq));

    delayed_queue_destroy(dq);
}

/* ====== Demo 4: Graceful Shutdown ====== */
static void long_running_task(void *userdata)
{
    volatile int *done = (volatile int *)userdata;
    int i;
    for (i = 0; i < 20; i++) {
        if (*done == -1) break;
        ms_sleep(50);
    }
    (*done)++;
    g_done++;
}

static void demo_graceful_shutdown(void)
{
    printf("\n========== ADVANCED DEMO 4: Graceful Shutdown ==========\n\n");

    wp_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_workers = 3;
    cfg.queue_size  = 16;
    cfg.graceful_shutdown_timeout_ms = 5000;

    worker_pool_t *pool = worker_pool_create(&cfg);

    int flags[10];
    int i;
    g_done = 0;

    for (i = 0; i < 10; i++) {
        flags[i] = 0;
        wp_submit(pool, 5000 + i, long_running_task, &flags[i],
                  NULL, NULL, "graceful_test");
    }

    printf("  Submitted 10 long-running tasks. Workers: %d\n", cfg.max_workers);

    ms_sleep(200);
    printf("  Busy workers: %d, Pending: %d\n",
           wp_busy_workers(pool), wp_pending_tasks(pool));

    printf("  Initiating graceful shutdown (timeout=3000ms)...\n");
    wp_shutdown_graceful(pool, 3000);

    printf("  After shutdown: busy=%d pending=%d done=%d\n",
           wp_busy_workers(pool), wp_pending_tasks(pool), g_done);

    worker_pool_destroy(pool);
}

/* ====== Demo 5: Extended Retry with Mixed Errors ====== */
static void demo_extended_retry(void)
{
    printf("\n========== ADVANCED DEMO 5: Extended Retry Patterns ==========\n\n");

    task_retry_t *tr = task_retry_create(32);
    int retry_flags[3] = {0, 0, 0};

    tr_register(tr, 6001, mixed_task, &retry_flags[0], 5, 100);
    tr_register(tr, 6002, mixed_task, &retry_flags[1], 2, 50);
    tr_register(tr, 6003, mixed_task, &retry_flags[2], 10, 500);

    tr_error_code_t codes_net[] = {TR_ERR_TIMEOUT, TR_ERR_CONN_REFUSED};
    tr_set_retry_on(tr, 6001, codes_net, 2);

    tr_error_code_t codes_all[] = {TR_ERR_TIMEOUT, TR_ERR_HTTP_503,
                                   TR_ERR_CONN_REFUSED, TR_ERR_TEMP_FAILURE};
    tr_set_retry_on(tr, 6003, codes_all, 4);

    printf("  Job 6001: max=5, base=100ms, retry on [timeout,conn_refused]\n");
    printf("  Job 6002: max=2, base=50ms,  retry on ALL errors\n");
    printf("  Job 6003: max=10,base=500ms, retry on ALL errors\n\n");

    tr_error_code_t sim_errors[] = {
        TR_ERR_TIMEOUT, TR_ERR_TIMEOUT, TR_ERR_CONN_REFUSED,
        TR_ERR_HTTP_503, TR_ERR_TEMP_FAILURE, TR_ERR_TIMEOUT
    };
    int se_count = sizeof(sim_errors) / sizeof(sim_errors[0]);
    int se;

    for (se = 0; se < se_count; se++) {
        printf("  --- Simulation step %d: ---\n", se + 1);
        printf("    Reporting to job 6001 (timeout): ");
        tr_report_result(tr, 6001, sim_errors[se]);
        printf("count=%d exhausted=%s\n",
               tr_get_retry_count(tr, 6001),
               tr_is_exhausted(tr, 6001) ? "yes" : "no");

        printf("    Reporting to job 6002: ");
        tr_report_result(tr, 6002, sim_errors[se]);
        printf("count=%d exhausted=%s\n",
               tr_get_retry_count(tr, 6002),
               tr_is_exhausted(tr, 6002) ? "yes" : "no");

        printf("    Reporting to job 6003: ");
        tr_report_result(tr, 6003, sim_errors[se]);
        printf("count=%d exhausted=%s\n",
               tr_get_retry_count(tr, 6003),
               tr_is_exhausted(tr, 6003) ? "yes" : "no");
    }

    printf("\n  Final retry counts: [%d, %d, %d]\n",
           tr_get_retry_count(tr, 6001),
           tr_get_retry_count(tr, 6002),
           tr_get_retry_count(tr, 6003));

    task_retry_destroy(tr);
}

/* ====== Demo 6: State Machine Walk ====== */
static void on_state_change(uint64_t job_id, jp_state_t old_s,
                            jp_state_t new_s, void *ud)
{
    const char *names[] = {"QUEUED","RUNNING","SUCCEEDED","FAILED","CANCELLED"};
    printf("    [State] %llu: %s -> %s\n",
           (unsigned long long)job_id, names[old_s], names[new_s]);
}

static void demo_state_machine(void)
{
    printf("\n========== ADVANCED DEMO 6: Full State Machine Walk ==========\n\n");

    job_progress_t *jp = job_progress_create();
    jp->job_id = 7001;
    jp_set_total_steps(jp, 3);
    jp_set_state_callback(jp, on_state_change, NULL);

    printf("  Walkthrough: QUEUED -> RUNNING -> ...\n\n");

    printf("  Step A - Initial:\n");
    printf("    state=%d, progress=%.1f%%\n",
           jp_get_state(jp), jp_get_progress(jp));

    printf("  Step B - Start job:\n");
    jp_transition_state(jp, JP_STATE_RUNNING, "job started");
    printf("    state=%d, elapsed=%llds\n",
           jp_get_state(jp), (long long)jp_get_elapsed(jp));

    printf("  Step C - Progress updates:\n");
    ms_sleep(100);
    jp_increment_progress(jp, "step 1");
    ms_sleep(120);
    jp_increment_progress(jp, "step 2");
    ms_sleep(80);
    jp_increment_progress(jp, "step 3");

    printf("  Step D - Complete:\n");
    jp_transition_state(jp, JP_STATE_SUCCEEDED, "finished successfully");
    printf("    state=%d, progress=%.1f%%, elapsed=%llds\n",
           jp_get_state(jp), jp_get_progress(jp),
           (long long)jp_get_elapsed(jp));

    printf("\n  Walkthrough: QUEUED -> RUNNING -> FAILED\n\n");

    job_progress_t *jp2 = job_progress_create();
    jp2->job_id = 7002;
    jp_set_total_steps(jp2, 5);
    jp_set_state_callback(jp2, on_state_change, NULL);

    jp_transition_state(jp2, JP_STATE_RUNNING, "started");
    jp_increment_progress(jp2, "step 1");
    jp_increment_progress(jp2, "step 2");
    ms_sleep(50);
    jp_fail(jp2, "encountered fatal error at step 3");
    printf("    After fail: state=%d progress=%.1f%%\n",
           jp_get_state(jp2), jp_get_progress(jp2));

    printf("\n  Walkthrough: QUEUED -> RUNNING -> CANCELLED\n\n");

    job_progress_t *jp3 = job_progress_create();
    jp3->job_id = 7003;
    jp_set_total_steps(jp3, 4);
    jp_set_state_callback(jp3, on_state_change, NULL);

    jp_transition_state(jp3, JP_STATE_RUNNING, "started");
    jp_increment_progress(jp3, "step 1");
    ms_sleep(30);
    jp_cancel(jp3, "user cancelled operation");
    printf("    After cancel: state=%d\n", jp_get_state(jp3));

    job_progress_destroy(jp);
    job_progress_destroy(jp2);
    job_progress_destroy(jp3);
}

/* ====== Main ====== */
int main(void)
{
    printf("=========================================\n");
    printf("  mini-job-system Advanced Demo\n");
    printf("=========================================\n");

    demo_pipeline();
    demo_cron_patterns();
    demo_delay_levels();
    demo_graceful_shutdown();
    demo_extended_retry();
    demo_state_machine();

    printf("\n=========================================\n");
    printf("  All Advanced Demos Complete.\n");
    printf("=========================================\n");
    return 0;
}
