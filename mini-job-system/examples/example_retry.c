#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "task_retry.h"

static int attempt_count = 0;

static void flaky_task(void *userdata)
{
    const char *name = (const char *)userdata;
    attempt_count++;
    printf("    [Attempt %d] %s executing...\n", attempt_count, name);
}

static void dead_letter_handler(uint64_t job_id, tr_error_code_t err,
                                void *userdata)
{
    const char *name = (const char *)userdata;
    printf("  [DEAD LETTER] job %llu (%s) failed with error %d\n",
           (unsigned long long)job_id, name, (int)err);
}

int main(void)
{
    printf("=== mini-job-system Task Retry Example ===\n\n");

    /* --- Exponential backoff calculation --- */
    printf("1. Backoff delay calculation:\n");
    {
        unsigned int seed = (unsigned int)time(NULL);
        int i;
        for (i = 0; i < 10; i++) {
            uint64_t delay = tr_calc_delay(i, 100, 1, &seed);
            printf("  retry %d -> %llu ms\n", i,
                   (unsigned long long)delay);
        }
    }

    /* --- Jitter demonstration --- */
    printf("\n2. Jitter effect (same retry, different seeds):\n");
    {
        int r;
        for (r = 0; r < 5; r++) {
            unsigned int seed = (unsigned int)(1000 + r);
            uint64_t delay = tr_calc_delay(2, 100, 1, &seed);
            printf("  run %d: %llu ms\n", r + 1,
                   (unsigned long long)delay);
        }
    }

    /* --- Retry on specific errors --- */
    printf("\n3. Selective retry (only timeout + 503):\n");
    {
        task_retry_t *tr = task_retry_create(16);

        attempt_count = 0;
        tr_register(tr, 5001, flaky_task, "api-call", 5, 100);

        tr_error_code_t on_codes[] = {TR_ERR_TIMEOUT, TR_ERR_HTTP_503};
        tr_set_retry_on(tr, 5001, on_codes, 2);

        tr_report_result(tr, 5001, TR_ERR_TIMEOUT);
        printf("  After timeout: can_retry=%s retry_count=%d\n",
               tr_can_retry(tr, 5001, time(NULL) + 1) ? "yes" : "no",
               tr_get_retry_count(tr, 5001));

        tr_report_result(tr, 5001, TR_ERR_CONN_REFUSED);
        printf("  After conn_refused (not in retry-on list): exhausted=%s\n",
               tr_is_exhausted(tr, 5001) ? "yes" : "no");

        task_retry_destroy(tr);
    }

    /* --- Max retries + dead letter queue --- */
    printf("\n4. Max retries + dead letter queue:\n");
    {
        task_retry_t *tr = task_retry_create(16);

        attempt_count = 0;
        tr_register(tr, 6001, flaky_task, "brittle-job", 3, 100);
        tr_set_dead_letter(tr, 6001, dead_letter_handler, "brittle-job");

        int attempt;
        for (attempt = 0; attempt < 6; attempt++) {
            printf("  Cycle %d:\n", attempt + 1);

            int can = tr_can_retry(tr, 6001, time(NULL) + 10);
            if (!can && tr_is_exhausted(tr, 6001)) {
                printf("    Exhausted after %d retries.\n",
                       tr_get_retry_count(tr, 6001));
                break;
            }

            if (can) {
                tr_trigger_retry(tr, 6001, time(NULL) + 10);
            }

            int will_retry = tr_report_result(tr, 6001,
                                              TR_ERR_TEMP_FAILURE);
            printf("    Retry queued: %s, count: %d\n",
                   will_retry ? "yes" : "no",
                   tr_get_retry_count(tr, 6001));
        }

        tr_process_dead_letters(tr);

        task_retry_destroy(tr);
    }

    /* --- No jitter vs jitter comparison --- */
    printf("\n5. Deterministic vs jittered delays:\n");
    {
        unsigned int seed = 42;
        int i;
        printf("  retries | no-jitter | with-jitter\n");
        printf("  --------|-----------|------------\n");
        for (i = 0; i < 8; i++) {
            uint64_t d = tr_calc_delay(i, 100, 0, NULL);
            uint64_t dj = tr_calc_delay(i, 100, 1, &seed);
            printf("  %7d | %9llu | %11llu\n", i,
                   (unsigned long long)d, (unsigned long long)dj);
        }
    }

    printf("\n=== Done ===\n");
    return 0;
}
