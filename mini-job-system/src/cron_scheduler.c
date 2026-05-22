#include "cron_scheduler.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

struct cron_scheduler_t {
    cron_job_t jobs[CRON_MAX_JOBS];
    int        job_count;
    int        max_jobs;
};

cron_scheduler_t *cron_scheduler_create(const cron_config_t *config)
{
    cron_scheduler_t *sched = (cron_scheduler_t *)calloc(1, sizeof(*sched));
    if (!sched) return NULL;
    sched->max_jobs = (config && config->max_jobs > 0)
                      ? config->max_jobs : CRON_MAX_JOBS;
    if (sched->max_jobs > CRON_MAX_JOBS) sched->max_jobs = CRON_MAX_JOBS;
    return sched;
}

void cron_scheduler_destroy(cron_scheduler_t *sched)
{
    if (sched) free(sched);
}

static const int cron_min_vals[CRON_FIELD_COUNT] = {0, 0, 0, 1, 1, 0};
static const int cron_max_vals[CRON_FIELD_COUNT] = {59, 59, 23, 31, 12, 6};

static int parse_list(cron_field_def_t *field, const char *str,
                      int min_val, int max_val)
{
    char  buf[256];
    char *token, *ctx = NULL;
    int   n;

    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    token = strtok_r(buf, ",", &ctx);
    while (token && field->count < 64) {
        char *slash = strchr(token, '/');
        char *dash  = strchr(token, '-');

        if (slash) {
            *slash = '\0';
            int step = atoi(slash + 1);
            if (step <= 0) step = 1;
            int lo = min_val, hi = max_val;
            if (strcmp(token, "*") != 0) {
                if (dash) {
                    *dash = '\0';
                    lo = atoi(token);
                    hi = atoi(dash + 1);
                } else {
                    lo = atoi(token);
                }
            }
            field->type = CRON_ENTRY_STEP;
            for (n = lo; n <= hi && field->count < 64; n += step)
                field->values[field->count++] = n;
        } else if (dash) {
            *dash = '\0';
            int lo = atoi(token);
            int hi = atoi(dash + 1);
            field->type = CRON_ENTRY_RANGE;
            for (n = lo; n <= hi && field->count < 64; n++)
                field->values[field->count++] = n;
        } else if (strcmp(token, "*") == 0) {
            field->type = CRON_ENTRY_ANY;
            field->count = 0;
        } else {
            if (field->type != CRON_ENTRY_SPECIFIC) {
                field->type = CRON_ENTRY_SPECIFIC;
                field->count = 0;
            }
            field->values[field->count++] = atoi(token);
        }
        token = strtok_r(NULL, ",", &ctx);
    }
    return 0;
}

int cron_parse_field(cron_field_def_t *field, const char *str,
                     int min_val, int max_val)
{
    memset(field, 0, sizeof(*field));
    field->type = CRON_ENTRY_ANY;
    if (!str || !*str || strcmp(str, "*") == 0)
        return 0;
    return parse_list(field, str, min_val, max_val);
}

int cron_parse_expression(cron_expression_t *expr, const char *spec)
{
    char  buf[256];
    char *tokens[CRON_FIELD_COUNT];
    char *token, *ctx = NULL;
    int   i;

    memset(expr, 0, sizeof(*expr));
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    token = strtok_r(buf, " \t", &ctx);
    for (i = 0; i < CRON_FIELD_COUNT && token; i++) {
        tokens[i] = token;
        token = strtok_r(NULL, " \t", &ctx);
    }

    if (i < 5) return -1;

    if (i == 5) {
        tokens[5] = tokens[4];
        tokens[4] = tokens[3];
        tokens[3] = tokens[2];
        tokens[2] = tokens[1];
        tokens[1] = tokens[0];
        tokens[0] = "0";
    }

    for (i = 0; i < CRON_FIELD_COUNT; i++) {
        int r = cron_parse_field(&expr->fields[i], tokens[i],
                                 cron_min_vals[i], cron_max_vals[i]);
        if (r < 0) return r;
    }
    return 0;
}

int cron_field_matches(const cron_field_def_t *field, int value)
{
    int i;
    if (field->type == CRON_ENTRY_ANY)
        return 1;
    for (i = 0; i < field->count; i++)
        if (field->values[i] == value)
            return 1;
    return 0;
}

int cron_matches(const cron_expression_t *expr, const struct tm *tm_val)
{
    int vals[CRON_FIELD_COUNT];
    vals[CRON_FIELD_SECOND]  = tm_val->tm_sec;
    vals[CRON_FIELD_MINUTE]  = tm_val->tm_min;
    vals[CRON_FIELD_HOUR]    = tm_val->tm_hour;
    vals[CRON_FIELD_DAY]     = tm_val->tm_mday;
    vals[CRON_FIELD_MONTH]   = tm_val->tm_mon + 1;
    vals[CRON_FIELD_WEEKDAY] = tm_val->tm_wday;

    int i;
    for (i = 0; i < CRON_FIELD_COUNT; i++)
        if (!cron_field_matches(&expr->fields[i], vals[i]))
            return 0;
    return 1;
}

static int cron_field_next(const cron_field_def_t *field, int val,
                           int min_val, int max_val)
{
    if (field->type == CRON_ENTRY_ANY)
        return val + 1;

    int i;
    for (i = 0; i < field->count; i++)
        if (field->values[i] > val)
            return field->values[i];
    return -1;
}

static int cron_field_first(const cron_field_def_t *field,
                            int min_val, int max_val)
{
    if (field->type == CRON_ENTRY_ANY)
        return min_val;
    if (field->count > 0)
        return field->values[0];
    return min_val;
}

time_t cron_next_fire_time(const cron_expression_t *expr, time_t from)
{
    struct tm tm_val;
    time_t    candidate = from + 1;
    int       iter;

#ifdef _WIN32
    struct tm *ptm = localtime(&candidate);
    if (!ptm) return (time_t)-1;
    tm_val = *ptm;
#else
    localtime_r(&candidate, &tm_val);
#endif

    for (iter = 0; iter < 525600 * 5; iter++) {
        if (cron_matches(expr, &tm_val))
            return mktime(&tm_val);

        int carry = 0;
        int sec = cron_field_next(&expr->fields[CRON_FIELD_SECOND],
                                   tm_val.tm_sec, 0, 59);
        if (sec < 0) { sec = cron_field_first(&expr->fields[CRON_FIELD_SECOND], 0, 59); carry = 1; }

        tm_val.tm_sec = sec;
        if (carry) tm_val.tm_min++;

        if (tm_val.tm_sec >= 60) { tm_val.tm_sec = 0; tm_val.tm_min++; }
        if (tm_val.tm_min >= 60) { tm_val.tm_min = 0; tm_val.tm_hour++; }
        if (tm_val.tm_hour >= 24) { tm_val.tm_hour = 0; tm_val.tm_mday++; }
        if (tm_val.tm_mday > 31)  { tm_val.tm_mday = 1;  tm_val.tm_mon++; }
        if (tm_val.tm_mon > 11)   { tm_val.tm_mon = 0;   tm_val.tm_year++; }

        candidate = mktime(&tm_val);
        if (candidate == (time_t)-1) break;
    }
    return (time_t)-1;
}

int cron_register_job(cron_scheduler_t *sched, uint64_t id,
                      const char *cron_spec, cron_job_fn cb, void *ud,
                      int catch_up)
{
    int i;
    if (!sched || !cron_spec || !cb) return -1;
    if (sched->job_count >= sched->max_jobs) return -2;

    for (i = 0; i < sched->job_count; i++)
        if (sched->jobs[i].id == id)
            return cron_unregister_job(sched, id);

    cron_job_t job;
    memset(&job, 0, sizeof(job));
    job.id       = id;
    job.callback = cb;
    job.userdata = ud;
    job.catch_up = catch_up;
    job.active   = 1;

    if (cron_parse_expression(&job.expr, cron_spec) < 0)
        return -3;

    job.next_fire = cron_next_fire_time(&job.expr, time(NULL));
    job.last_fire = 0;

    sched->jobs[sched->job_count++] = job;
    return 0;
}

int cron_unregister_job(cron_scheduler_t *sched, uint64_t id)
{
    int i;
    if (!sched) return -1;
    for (i = 0; i < sched->job_count; i++) {
        if (sched->jobs[i].id == id) {
            sched->jobs[i] = sched->jobs[sched->job_count - 1];
            sched->job_count--;
            return 0;
        }
    }
    return -1;
}

int cron_activate_job(cron_scheduler_t *sched, uint64_t id, int active)
{
    int i;
    if (!sched) return -1;
    for (i = 0; i < sched->job_count; i++) {
        if (sched->jobs[i].id == id) {
            sched->jobs[i].active = active;
            if (active)
                sched->jobs[i].next_fire = cron_next_fire_time(
                    &sched->jobs[i].expr, time(NULL));
            return 0;
        }
    }
    return -1;
}

int cron_tick(cron_scheduler_t *sched, time_t now)
{
    int i, fired = 0;
    if (!sched) return -1;

    for (i = 0; i < sched->job_count; i++) {
        cron_job_t *job = &sched->jobs[i];
        if (!job->active) continue;

        if (now >= job->next_fire) {
            int  missed = 0;
            time_t next;

            if (job->catch_up) {
                next = cron_next_fire_time(&job->expr, job->next_fire);
                while (next <= now && next != (time_t)-1) {
                    missed++;
                    next = cron_next_fire_time(&job->expr, next);
                    if (next == (time_t)-1) break;
                }
                while (missed > 0) {
                    job->callback(job->userdata);
                    missed--;
                }
            }

            job->callback(job->userdata);
            job->last_fire = now;
            job->next_fire = cron_next_fire_time(&job->expr, now);
            if (job->next_fire == (time_t)-1)
                job->active = 0;
            fired++;
        }
    }
    return fired;
}

void cron_run_pending(cron_scheduler_t *sched)
{
    cron_tick(sched, time(NULL));
}
