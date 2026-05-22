#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cron_scheduler.h"
#include "delayed_queue.h"
#include "worker_pool.h"
#include "task_retry.h"
#include "job_progress.h"

static void sample_task(void *userdata)
{
    const char *name = (const char *)userdata;
    printf("  [Task] %s executed at %lld\n", name,
           (long long)time(NULL));
}

static void sample_ack(uint64_t job_id, int status, void *userdata)
{
    printf("  [ACK] job %llu status=%d\n",
           (unsigned long long)job_id, status);
}

static void on_progress(uint64_t job_id, int done, int total,
                        double pct, void *ud)
{
    printf("  [Progress] job %llu: %d/%d (%.1f%%)\n",
           (unsigned long long)job_id, done, total, pct);
}

static void on_state(uint64_t job_id, jp_state_t old_s,
                     jp_state_t new_s, void *ud)
{
    const char *names[] = {"QUEUED","RUNNING","SUCCEEDED","FAILED","CANCELLED"};
    printf("  [State] job %llu: %s -> %s\n",
           (unsigned long long)job_id, names[old_s], names[new_s]);
}

int main(void)
{
    printf("=== mini-job-system Basic Example ===\n\n");

    /* --- Async worker pool with job progress --- */
    printf("1. Worker pool + job progress:\n");
    {
        wp_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.max_workers = 2;
        cfg.queue_size  = 16;

        worker_pool_t *pool = worker_pool_create(&cfg);

        job_progress_t *jp = job_progress_create();
        jp->job_id = 1001;
        jp_set_total_steps(jp, 5);
        jp_set_state_callback(jp, on_state, NULL);
        jp_set_progress_callback(jp, on_progress, NULL);
        jp_transition_state(jp, JP_STATE_RUNNING, "started");

        wp_submit(pool, 1001, sample_task, "Task A",
                  sample_ack, NULL, "report");

        Sleep(100);
        jp_increment_progress(jp, "step 1 done");
        jp_increment_progress(jp, "step 2 done");
        jp_increment_progress(jp, "step 3 done");
        jp_increment_progress(jp, "step 4 done");
        jp_increment_progress(jp, "step 5 done");
        jp_transition_state(jp, JP_STATE_SUCCEEDED, "all done");

        printf("  Progress: %.1f%%, ETA: %.1fs, Elapsed: %llds\n",
               jp_get_progress(jp), jp_get_eta(jp),
               (long long)jp_get_elapsed(jp));

        int log_count;
        const jp_log_entry_t *log = jp_get_log(jp, &log_count);
        printf("  Log entries: %d\n", log_count);
        int li;
        for (li = 0; li < log_count; li++)
            printf("    [%lld] %s\n",
                   (long long)log[li].timestamp, log[li].message);

        wp_shutdown(pool);
        Sleep(200);
        worker_pool_destroy(pool);
        job_progress_destroy(jp);
    }

    /* --- Delayed queue with priority levels --- */
    printf("\n2. Delayed queue:\n");
    {
        delayed_queue_t *dq = delayed_queue_create(32, 1);
        dq_enqueue_level(dq, 1, "periodic_cleanup",
                         sample_task, "cleanup job", DQ_LEVEL_10S);
        dq_enqueue(dq, 2, "email_report", sample_task, "email job", 5000);

        printf("  Queue size: %d\n", dq_size(dq));
        printf("  Next delay: %llu ms\n",
               (unsigned long long)dq_next_delay_ms(dq, time(NULL)));

        dq_entry_t entry;
        if (dq_peek(dq, &entry))
            printf("  Peek: id=%llu fire=%lld\n",
                   (unsigned long long)entry.id,
                   (long long)entry.fire_time);

        int cancelled = dq_cancel_by_key(dq, "email_report");
        printf("  Cancelled 'email_report': %s\n",
               cancelled == 0 ? "yes" : "not found");

        delayed_queue_destroy(dq);
    }

    /* --- Retry with exponential backoff --- */
    printf("\n3. Task retry with backoff:\n");
    {
        task_retry_t *tr = task_retry_create(64);

        tr_register(tr, 3001, sample_task, "flaky task", 3, 100);
        tr_error_code_t retry_codes[] = {TR_ERR_TIMEOUT,
                                         TR_ERR_CONN_REFUSED,
                                         TR_ERR_HTTP_503};
        tr_set_retry_on(tr, 3001, retry_codes, 3);

        int attempt;
        for (attempt = 0; attempt < 5; attempt++) {
            printf("  Attempt %d: ", attempt + 1);

            int can = tr_can_retry(tr, 3001, time(NULL) + 10);
            if (!can && tr_is_exhausted(tr, 3001)) {
                printf("exhausted.\n");
                break;
            }

            if (!can) {
                printf("pending (next retry in future).\n");
                break;
            }

            if (attempt < 2) {
                printf("failed (timeout) -> will retry.\n");
                tr_report_result(tr, 3001, TR_ERR_TIMEOUT);
            } else {
                printf("success.\n");
                tr_report_result(tr, 3001, TR_ERR_NONE);
                break;
            }
        }

        printf("  Retry count: %d\n", tr_get_retry_count(tr, 3001));
        printf("  Exhausted: %s\n",
               tr_is_exhausted(tr, 3001) ? "yes" : "no");

        task_retry_destroy(tr);
    }

    printf("\n=== Done ===\n");
    return 0;
}
