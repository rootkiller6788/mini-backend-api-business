#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cron_scheduler.h"
#include "delayed_queue.h"
#include "worker_pool.h"
#include "task_retry.h"
#include "job_progress.h"

/* ====== Demo Configuration ====== */
#define DEMO_WORKERS       4
#define DEMO_QUEUE_SIZE    64
#define DEMO_JOB_COUNT     10

typedef struct {
    int  id;
    char name[64];
    int  steps;
    int  step_ms;
} demo_job_config_t;

typedef struct {
    worker_pool_t    *pool;
    delayed_queue_t  *dq;
    task_retry_t     *retry;
    cron_scheduler_t *cron;
    volatile int      active_jobs;
    volatile int      total_completed;
    job_progress_t   *progresses[DEMO_JOB_COUNT];
} demo_system_t;

static demo_system_t g_sys;

/* ----- utility ----- */
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

/* ----- progress callback ----- */
static void demo_progress_cb(uint64_t job_id, int done, int total,
                             double pct, void *ud)
{
    int bar_len = 20;
    int filled  = (int)(pct / 100.0 * (double)bar_len);
    int i;
    printf("\r  [Job %-3llu] [", (unsigned long long)job_id);
    for (i = 0; i < bar_len; i++)
        putchar(i < filled ? '#' : '-');
    printf("] %5.1f%% (%d/%d)", pct, done, total);
    if (pct >= 100.0) printf("\n");
    fflush(stdout);
}

static void demo_state_cb(uint64_t job_id, jp_state_t old_s,
                          jp_state_t new_s, void *ud)
{
    const char *names[] = {"QUEUED","RUNNING","SUCCEEDED","FAILED","CANCELLED"};
    printf("\n  --- Job %llu: %s -> %s ---\n",
           (unsigned long long)job_id, names[old_s], names[new_s]);
}

/* ----- worker task (simulates work) ----- */
typedef struct {
    uint64_t job_id;
    int      steps;
    int      step_ms;
    job_progress_t *jp;
} demo_task_ctx_t;

static void demo_worker_task(void *userdata)
{
    demo_task_ctx_t *ctx = (demo_task_ctx_t *)userdata;
    int step;

    jp_transition_state(ctx->jp, JP_STATE_RUNNING, "worker started");

    for (step = 1; step <= ctx->steps; step++) {
        ms_sleep(ctx->step_ms);
        char msg[128];
        sprintf(msg, "step %d/%d done", step, ctx->steps);
        jp_increment_progress(ctx->jp, msg);

        if (step == ctx->steps) {
            jp_transition_state(ctx->jp, JP_STATE_SUCCEEDED, "completed");
        }
    }
}

/* ----- delayed job callback ----- */
static void demo_delayed_cb(void *userdata)
{
    const char *msg = (const char *)userdata;
    printf("\n  [Delayed] %s\n", msg);
}

/* ----- cron job callback ----- */
static void demo_cron_cb(void *userdata)
{
    const char *msg = (const char *)userdata;
    time_t now = time(NULL);
    printf("\n  [Cron %lld] %s\n", (long long)now, msg);
}

/* ----- retry dead letter callback ----- */
static void demo_dead_cb(uint64_t job_id, tr_error_code_t err, void *ud)
{
    printf("\n  [DEAD-LETTER] Job %llu permanently failed (err=%d)\n",
           (unsigned long long)job_id, (int)err);
}

/* ----- ack callback ----- */
static void demo_ack_cb(uint64_t job_id, int status, void *ud)
{
    demo_system_t *sys = (demo_system_t *)ud;
    printf("  [ACK] Job %llu acknowledged (status=%d)\n",
           (unsigned long long)job_id, status);
    sys->total_completed++;
}

/* ----- init full system ----- */
static int demo_system_init(void)
{
    memset(&g_sys, 0, sizeof(g_sys));

    wp_config_t wcfg;
    memset(&wcfg, 0, sizeof(wcfg));
    wcfg.max_workers = DEMO_WORKERS;
    wcfg.queue_size  = DEMO_QUEUE_SIZE;
    wcfg.graceful_shutdown_timeout_ms = 5000;

    g_sys.pool = worker_pool_create(&wcfg);
    if (!g_sys.pool) return -1;

    g_sys.dq = delayed_queue_create(64, 1);
    if (!g_sys.dq) return -2;

    g_sys.retry = task_retry_create(64);
    if (!g_sys.retry) return -3;

    cron_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.max_jobs = 8;
    g_sys.cron = cron_scheduler_create(&ccfg);
    if (!g_sys.cron) return -4;

    int i;
    for (i = 0; i < DEMO_JOB_COUNT; i++) {
        g_sys.progresses[i] = NULL;
    }

    return 0;
}

static void demo_system_cleanup(void)
{
    wp_shutdown_graceful(g_sys.pool, 3000);
    ms_sleep(500);
    worker_pool_destroy(g_sys.pool);

    delayed_queue_destroy(g_sys.dq);
    task_retry_destroy(g_sys.retry);
    cron_scheduler_destroy(g_sys.cron);

    int i;
    for (i = 0; i < DEMO_JOB_COUNT; i++)
        job_progress_destroy(g_sys.progresses[i]);
}

/* ====== Demo: Submit batch of jobs ====== */
static void demo_submit_batch(void)
{
    printf("\n========== DEMO 1: Submit Batch Jobs ==========\n\n");

    demo_job_config_t configs[] = {
        {1, "data-export",    20, 25},
        {2, "report-gen",     10, 30},
        {3, "email-campaign", 15, 20},
        {4, "cache-warmup",    5, 40},
        {5, "video-transcode", 25, 15},
    };
    int count = sizeof(configs) / sizeof(configs[0]);
    int i;

    wp_set_concurrency_limit(g_sys.pool, "heavy", 1);

    wp_task_t batch[5];
    for (i = 0; i < count; i++) {
        g_sys.progresses[i] = job_progress_create();
        job_progress_t *jp = g_sys.progresses[i];
        jp->job_id = configs[i].id;
        jp_set_total_steps(jp, configs[i].steps);
        jp_set_progress_callback(jp, demo_progress_cb, NULL);
        jp_set_state_callback(jp, demo_state_cb, NULL);

        demo_task_ctx_t *ctx = (demo_task_ctx_t *)malloc(sizeof(*ctx));
        ctx->job_id  = configs[i].id;
        ctx->steps   = configs[i].steps;
        ctx->step_ms = configs[i].step_ms;
        ctx->jp       = jp;

        jp_transition_state(jp, JP_STATE_QUEUED, "queued");

        batch[i].job_id   = configs[i].id;
        batch[i].task     = demo_worker_task;
        batch[i].userdata = ctx;
        batch[i].ack      = demo_ack_cb;
        batch[i].ack_ud   = &g_sys;
        batch[i].job_type = "standard";

        printf("  Submitted job %d: %s (%d steps)\n",
               configs[i].id, configs[i].name, configs[i].steps);
    }

    int submitted = wp_submit_batch(g_sys.pool, batch, count);
    printf("  Batch submitted: %d tasks\n", submitted);

    while (wp_busy_workers(g_sys.pool) > 0 || wp_pending_tasks(g_sys.pool) > 0) {
        printf("  [STATUS] busy=%d pending=%d completed=%d\n",
               wp_busy_workers(g_sys.pool),
               wp_pending_tasks(g_sys.pool),
               g_sys.total_completed);
        ms_sleep(200);
    }

    printf("  All batch jobs finished. Total completed: %d\n\n",
           g_sys.total_completed);

    for (i = 0; i < count; i++) free(batch[i].userdata);
}

/* ====== Demo: Delayed + Cron ====== */
static void demo_delayed_and_cron(void)
{
    printf("========== DEMO 2: Delayed Queue + Cron Scheduler ==========\n\n");

    printf("  Enqueuing delayed jobs:\n");
    dq_enqueue_level(g_sys.dq, 100, "reminder_10s",
                     demo_delayed_cb, "10-second reminder", DQ_LEVEL_10S);
    printf("    - reminder_10s (10s delay)\n");

    dq_enqueue_level(g_sys.dq, 101, "cleanup_1min",
                     demo_delayed_cb, "1-minute cleanup", DQ_LEVEL_1MIN);
    printf("    - cleanup_1min (1min delay)\n");

    dq_enqueue(g_sys.dq, 102, "quick_ping",
               demo_delayed_cb, "quick ping (1.5s)", 1500);
    printf("    - quick_ping (1.5s delay)\n");

    printf("\n  Registering cron jobs:\n");
    cron_register_job(g_sys.cron, 200, "*/5 * * * * *",
                      demo_cron_cb, "every 5 seconds", 0);
    printf("    - every 5 seconds\n");

    cron_register_job(g_sys.cron, 201, "0 */1 * * * *",
                      demo_cron_cb, "every minute", 0);
    printf("    - every minute\n");

    printf("\n  Running scheduler for 8 seconds...\n");
    time_t start = time(NULL);
    dq_entry_t entry;

    while (difftime(time(NULL), start) < 8.0) {
        cron_run_pending(g_sys.cron);

        while (dq_dequeue(g_sys.dq, time(NULL), &entry)) {
            printf("  [DQ-FIRE] id=%llu, key=%s\n",
                   (unsigned long long)entry.id, entry.key);
            entry.callback(entry.userdata);
        }

        printf("  [Tick] dq_size=%d, next_delay=%llums\n",
               dq_size(g_sys.dq),
               (unsigned long long)dq_next_delay_ms(g_sys.dq, time(NULL)));
        ms_sleep(1000);
    }

    printf("  Scheduler loop ended.\n\n");
}

/* ====== Demo: Retry with backoff ====== */
static void demo_retry_scenario(void)
{
    printf("========== DEMO 3: Retry with Exponential Backoff ==========\n\n");

    tr_register(g_sys.retry, 300, demo_worker_task, NULL, 4, 200);
    tr_error_code_t codes[] = {TR_ERR_TIMEOUT, TR_ERR_HTTP_503,
                               TR_ERR_CONN_REFUSED};
    tr_set_retry_on(g_sys.retry, 300, codes, 3);
    tr_set_dead_letter(g_sys.retry, 300, demo_dead_cb, NULL);

    printf("  Job 300 registered (max 4 retries, base 200ms)\n\n");

    tr_error_code_t errors[] = {
        TR_ERR_TIMEOUT,
        TR_ERR_CONN_REFUSED,
        TR_ERR_HTTP_503,
        TR_ERR_TEMP_FAILURE,
        TR_ERR_TIMEOUT,
    };
    int num_errors = sizeof(errors) / sizeof(errors[0]);
    int ei;

    for (ei = 0; ei < num_errors; ei++) {
        printf("  --- Cycle %d (reporting error %d) ---\n",
               ei + 1, (int)errors[ei]);

        int will_retry = tr_report_result(g_sys.retry, 300, errors[ei]);
        printf("    Will retry: %s\n", will_retry ? "yes" : "no");
        printf("    Retry count: %d\n", tr_get_retry_count(g_sys.retry, 300));
        printf("    Is exhausted: %s\n",
               tr_is_exhausted(g_sys.retry, 300) ? "yes" : "no");

        time_t future = time(NULL) + 10;
        if (tr_can_retry(g_sys.retry, 300, future) && !tr_is_exhausted(g_sys.retry, 300)) {
            tr_trigger_retry(g_sys.retry, 300, future);
            printf("    Retry triggered.\n");
        }

        if (tr_is_exhausted(g_sys.retry, 300)) {
            printf("    -> JOB EXHAUSTED, moving to dead letter\n");
            tr_process_dead_letters(g_sys.retry);
            break;
        }
        printf("\n");
    }

    printf("  Retry scenario complete.\n\n");
}

/* ====== Demo: Progress tracking + ETA ====== */
static void demo_progress_eta(void)
{
    printf("========== DEMO 4: Progress Tracking + ETA ==========\n\n");

    job_progress_t *jp = job_progress_create();
    jp->job_id = 999;
    jp_set_total_steps(jp, 100);
    jp_transition_state(jp, JP_STATE_RUNNING, "long job started");

    int step;
    for (step = 1; step <= 100; step++) {
        jp_update_progress(jp, step, NULL);
        ms_sleep(20);

        if (step % 20 == 0) {
            printf("  Step %3d/100 | Progress: %5.1f%% | ETA: %6.1fs | "
                   "Elapsed: %4llds | Avg: %.1fms/step\n",
                   step, jp_get_progress(jp), jp_get_eta(jp),
                   (long long)jp_get_elapsed(jp), jp->avg_step_ms);
        }
    }

    jp_transition_state(jp, JP_STATE_SUCCEEDED, "long job done");

    int log_count;
    const jp_log_entry_t *log = jp_get_log(jp, &log_count);
    printf("\n  Final log (%d entries):\n", log_count);
    int li;
    for (li = 0; li < log_count && li < 10; li++) {
        printf("    [%lld] step=%d pct=%.1f%% msg=%s\n",
               (long long)log[li].timestamp,
               log[li].completed, log[li].pct,
               log[li].message[0] ? log[li].message : "(none)");
    }

    printf("\n  Final state: %s\n",
           jp_get_state(jp) == JP_STATE_SUCCEEDED ? "SUCCEEDED" : "OTHER");
    job_progress_destroy(jp);
}

/* ====== Demo: Concurrency Control ====== */
static void demo_concurrency(void)
{
    printf("\n========== DEMO 5: Per-type Concurrency Control ==========\n\n");

    wp_set_concurrency_limit(g_sys.pool, "heavy", 1);
    wp_set_concurrency_limit(g_sys.pool, "light", 4);

    printf("  Limits: heavy=1, light=4\n\n");

    int i;
    for (i = 0; i < 6; i++) {
        job_progress_t *jp = job_progress_create();
        jp->job_id = 3000 + i;
        jp_set_total_steps(jp, 3);
        jp_set_state_callback(jp, demo_state_cb, NULL);

        char *ctx_name = (char *)malloc(32);
        sprintf(ctx_name, "heavy-task-%d", i);
        demo_task_ctx_t *ctx = (demo_task_ctx_t *)malloc(sizeof(*ctx));
        ctx->job_id  = 3000 + i;
        ctx->steps   = 3;
        ctx->step_ms = 200;
        ctx->jp      = jp;

        const char *jtype = (i < 3) ? "heavy" : "light";
        wp_submit(g_sys.pool, 3000 + i, demo_worker_task,
                  ctx, demo_ack_cb, &g_sys, jtype);

        printf("  Submitted %s job %d\n", jtype, 3000 + i);
    }

    ms_sleep(100);
    printf("\n  Waiting for concurrency-controlled execution...\n");

    while (wp_busy_workers(g_sys.pool) > 0 || wp_pending_tasks(g_sys.pool) > 0) {
        printf("  [STATUS] busy=%d pending=%d\n",
               wp_busy_workers(g_sys.pool),
               wp_pending_tasks(g_sys.pool));
        ms_sleep(300);
    }
}

/* ====== Main ====== */
int main(void)
{
    printf("=========================================\n");
    printf("  mini-job-system Full Demo\n");
    printf("=========================================\n");

    if (demo_system_init() < 0) {
        printf("Failed to initialize demo system.\n");
        return 1;
    }
    printf("System initialized: %d workers, queue=%d\n",
           DEMO_WORKERS, DEMO_QUEUE_SIZE);

    demo_submit_batch();
    demo_delayed_and_cron();
    demo_retry_scenario();
    demo_progress_eta();
    demo_concurrency();

    printf("\n=========================================\n");
    printf("  Demo Complete. Cleaning up...\n");
    printf("=========================================\n");

    demo_system_cleanup();

    printf("Done.\n");
    return 0;
}
