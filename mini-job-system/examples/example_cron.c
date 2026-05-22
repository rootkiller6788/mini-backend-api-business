#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cron_scheduler.h"

static void report_job(void *userdata)
{
    const char *name = (const char *)userdata;
    time_t now = time(NULL);
    struct tm tm_val;
#ifdef _WIN32
    struct tm *ptm = localtime(&now);
    if (ptm) tm_val = *ptm;
#else
    localtime_r(&now, &tm_val);
#endif
    printf("  [CRON] %s fired at %04d-%02d-%02d %02d:%02d:%02d\n",
           name, tm_val.tm_year + 1900, tm_val.tm_mon + 1,
           tm_val.tm_mday, tm_val.tm_hour,
           tm_val.tm_min, tm_val.tm_sec);
}

static void cleanup_job(void *userdata)
{
    printf("  [CRON] Running hourly cleanup...\n");
}

int main(void)
{
    printf("=== mini-job-system Cron Scheduler Example ===\n\n");

    /* --- Parsing cron expressions --- */
    printf("1. Cron expression parsing:\n");
    {
        const char *tests[] = {
            "* * * * * *",
            "0 */5 * * * *",
            "30 9-17 * * 1-5",
            "0,15,30,45 0 * * *",
            "*/10 */2 * * * *",
            "0 0 1 1,6 *",
            NULL
        };

        int ti;
        for (ti = 0; tests[ti]; ti++) {
            cron_expression_t expr;
            int r = cron_parse_expression(&expr, tests[ti]);
            printf("  \"%s\" -> %s\n", tests[ti],
                   r == 0 ? "OK" : "FAIL");
        }
    }

    /* --- Field-level matching --- */
    printf("\n2. Field matching:\n");
    {
        cron_field_def_t field;
        cron_parse_field(&field, "1-5,10,15", 0, 59);
        int vals[] = {0, 1, 3, 5, 7, 10, 15, 20};
        int vi;
        for (vi = 0; vi < 8; vi++) {
            int m = cron_field_matches(&field, vals[vi]);
            printf("  match(value=%d) = %s\n", vals[vi],
                   m ? "yes" : "no");
        }
    }

    /* --- Next fire time calculation --- */
    printf("\n3. Next fire time calculation:\n");
    {
        cron_expression_t expr;
        const char *specs[] = {
            "0 0 * * * *",
            "0 */15 * * * *",
            "0 9 * * 1-5",
            NULL
        };
        int si;
        for (si = 0; specs[si]; si++) {
            cron_parse_expression(&expr, specs[si]);
            time_t now = time(NULL);
            time_t next = cron_next_fire_time(&expr, now);
            struct tm tm_val;
#ifdef _WIN32
            struct tm *ptm = localtime(&next);
            if (ptm) tm_val = *ptm;
#else
            localtime_r(&next, &tm_val);
#endif
            printf("  \"%s\" next fire: %04d-%02d-%02d %02d:%02d:%02d\n",
                   specs[si],
                   tm_val.tm_year + 1900, tm_val.tm_mon + 1,
                   tm_val.tm_mday, tm_val.tm_hour,
                   tm_val.tm_min, tm_val.tm_sec);
        }
    }

    /* --- Cron scheduler with job registration --- */
    printf("\n4. Cron scheduler (register + tick):\n");
    {
        cron_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.max_jobs = 8;

        cron_scheduler_t *sched = cron_scheduler_create(&cfg);

        cron_register_job(sched, 1, "*/5 * * * * *",
                          report_job, "5sec report", 0);
        cron_register_job(sched, 2, "0 0 * * *",
                          cleanup_job, NULL, 0);
        cron_register_job(sched, 3, "0 30 9 * * 1-5",
                          report_job, "weekday 9:30", 0);

        printf("  Registered 3 cron jobs.\n");

        cron_job_t *cj;
        int registered = 3;
        printf("  Registered jobs: %d\n", registered);

        int fired = cron_tick(sched, time(NULL));
        printf("  Jobs triggered this tick: %d\n", fired);

        cron_activate_job(sched, 2, 0);
        printf("  Deactivated job 2 (hourly cleanup).\n");

        cron_unregister_job(sched, 3);
        printf("  Unregistered job 3.\n");

        cron_scheduler_destroy(sched);
    }

    /* --- Catch-up mode simulation --- */
    printf("\n5. Catch-up mode:\n");
    {
        cron_scheduler_t *sched = cron_scheduler_create(NULL);
        cron_register_job(sched, 100, "*/2 * * * * *",
                          report_job, "catch-up job", 1);

        time_t past   = time(NULL) - 30;
        int    missed = cron_tick(sched, time(NULL));
        printf("  Catch-up triggered %d firings (including missed).\n",
               missed);

        cron_scheduler_destroy(sched);
    }

    printf("\n=== Done ===\n");
    return 0;
}
