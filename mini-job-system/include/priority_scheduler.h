#ifndef PRIORITY_SCHEDULER_H
#define PRIORITY_SCHEDULER_H

#include <stdint.h>
#include <time.h>

/*
 * L2 Core Concept: Priority-Based Scheduling
 * Assigns execution order based on priority values. Higher priority jobs
 * execute before lower priority ones.
 *
 * L3 Engineering Structure: Multi-Level Feedback Queue (MLFQ)
 *   - Multiple priority levels, each with its own ready queue
 *   - Jobs can be promoted/demoted based on behavior (feedback)
 *   - Time-slice quantum per level
 *
 * L4 Standards/Theorems:
 *   - Priority Inversion (Lampson & Redell 1980): A low-priority job holds
 *     a resource needed by a high-priority job, which is blocked by a
 *     medium-priority job. Solved via priority inheritance.
 *   - Liu & Layland (1973): Rate Monotonic Scheduling (RMS) optimal for
 *     static priorities in periodic real-time systems.
 *   - Little's Law: L = lambda * W applies to queue length prediction.
 *
 * L5 Algorithms:
 *   - Multi-Level Feedback Queue (MLFQ) scheduler
 *   - Priority inheritance protocol (basic)
 *   - Aging: prevent starvation by increasing priority over time
 *   - Lottery scheduling: weighted random selection for fairness
 *
 * Reference:
 *   - MIT 6.828: Operating System Engineering (scheduling)
 *   - CMU 15-410: Operating System Design (priority inversion case study)
 *   - Lampson & Redell (1980) "Experience with Processes and Monitors in Mesa"
 *   - Waldspurger & Weihl (1994) "Lottery Scheduling"
 */

#define PS_MAX_PRIORITY_LEVELS  8
#define PS_MAX_JOBS_PER_LEVEL   64
#define PS_MAX_JOBS             256

typedef void (*ps_job_fn)(void *userdata);

typedef enum {
    PS_SCHED_MLFQ   = 0,   /* Multi-Level Feedback Queue */
    PS_SCHED_FIXED  = 1,   /* Fixed priority (no aging) */
    PS_SCHED_LOTTERY = 2   /* Lottery scheduling */
} ps_sched_algo_t;

/* Priority level description */
typedef struct {
    int   priority;       /* 0 = highest */
    int   quantum_ms;     /* time quantum for this level */
    int   allocation;     /* percent of CPU time (for guaranteed scheduling) */
} ps_priority_level_t;

/* Job entry */
typedef struct {
    uint64_t   id;
    ps_job_fn  job;
    void      *userdata;
    int        priority;
    int        original_priority;  /* for priority inheritance */
    int        boost_count;        /* times promoted via aging */
    int        tickets;            /* for lottery scheduling */
    time_t     enqueued_at;
    int        active;
} ps_job_entry_t;

/* Opaque scheduler */
typedef struct ps_scheduler_t ps_scheduler_t;

/* Configuration */
typedef struct {
    ps_sched_algo_t      algorithm;
    ps_priority_level_t  levels[PS_MAX_PRIORITY_LEVELS];
    int                  level_count;
    int                  aging_interval_sec;  /* how often to boost priority */
    int                  max_aging_boosts;
} ps_config_t;

/* Lifecycle */
ps_scheduler_t *ps_create(const ps_config_t *config);
void            ps_destroy(ps_scheduler_t *sched);

/* Job management */
int  ps_submit(ps_scheduler_t *sched, uint64_t id, ps_job_fn job,
               void *ud, int priority, int tickets);
int  ps_cancel(ps_scheduler_t *sched, uint64_t id);
int  ps_boost_priority(ps_scheduler_t *sched, uint64_t id, int new_priority);
int  ps_restore_priority(ps_scheduler_t *sched, uint64_t id);

/* Scheduling */
int  ps_schedule_next(ps_scheduler_t *sched, ps_job_entry_t *out);  /* get next job */
int  ps_schedule_next_timed(ps_scheduler_t *sched, ps_job_entry_t *out,
                             int timeout_ms);                       /* with timeout */

/* Priority inheritance: boost low-priority job holding a resource needed
 * by a high-priority job to avoid priority inversion. */
int  ps_priority_inherit(ps_scheduler_t *sched, uint64_t holder_id,
                          uint64_t waiter_id);

/* Aging: increase priority of long-waiting jobs to prevent starvation.
 * O(N) where N = number of enqueued jobs. */
int  ps_age_jobs(ps_scheduler_t *sched, time_t now);

/* Query */
int  ps_queue_size(const ps_scheduler_t *sched);
int  ps_queue_size_at(const ps_scheduler_t *sched, int priority);
int  ps_is_empty(const ps_scheduler_t *sched);

/* Lottery scheduling: select a job randomly weighted by tickets.
 * O(N) pass over all active jobs.
 * Returns index in the internal array, or -1 if empty. */
int  ps_lottery_select(ps_scheduler_t *sched, unsigned int *seed);

/* Statistics */
int    ps_total_submitted(const ps_scheduler_t *sched);
int    ps_total_completed(const ps_scheduler_t *sched);
double ps_avg_wait_time(const ps_scheduler_t *sched);

#endif
