#ifndef CRON_SCHEDULER_H
#define CRON_SCHEDULER_H

#include <stdint.h>
#include <time.h>

#define CRON_MAX_JOBS       64
#define CRON_FIELD_WILDCARD (-1)
#define CRON_FIELD_ANY       (-1)

typedef enum {
    CRON_FIELD_SECOND = 0,
    CRON_FIELD_MINUTE,
    CRON_FIELD_HOUR,
    CRON_FIELD_DAY,
    CRON_FIELD_MONTH,
    CRON_FIELD_WEEKDAY,
    CRON_FIELD_COUNT
} cron_field_t;

typedef enum {
    CRON_ENTRY_ANY      = 0,
    CRON_ENTRY_SPECIFIC,
    CRON_ENTRY_RANGE,
    CRON_ENTRY_STEP
} cron_entry_type_t;

typedef struct {
    cron_entry_type_t type;
    int               values[64];
    int               count;
} cron_field_def_t;

typedef struct {
    cron_field_def_t fields[CRON_FIELD_COUNT];
    int              catch_up;
} cron_expression_t;

typedef void (*cron_job_fn)(void *userdata);

typedef struct {
    uint64_t           id;
    cron_expression_t  expr;
    cron_job_fn        callback;
    void              *userdata;
    int                catch_up;
    time_t             last_fire;
    time_t             next_fire;
    int                active;
} cron_job_t;

typedef struct cron_scheduler_t cron_scheduler_t;

typedef struct {
    int  max_jobs;
    void *reserved;
} cron_config_t;

cron_scheduler_t *cron_scheduler_create(const cron_config_t *config);
void              cron_scheduler_destroy(cron_scheduler_t *sched);

int  cron_parse_expression(cron_expression_t *expr, const char *spec);
int  cron_parse_field(cron_field_def_t *field, const char *str, int min_val, int max_val);

int  cron_register_job(cron_scheduler_t *sched, uint64_t id,
                       const char *cron_spec, cron_job_fn cb, void *ud, int catch_up);
int  cron_unregister_job(cron_scheduler_t *sched, uint64_t id);
int  cron_activate_job(cron_scheduler_t *sched, uint64_t id, int active);

time_t cron_next_fire_time(const cron_expression_t *expr, time_t from);
int    cron_matches(const cron_expression_t *expr, const struct tm *tm_val);

int  cron_tick(cron_scheduler_t *sched, time_t now);
void cron_run_pending(cron_scheduler_t *sched);

int cron_field_matches(const cron_field_def_t *field, int value);

#endif
