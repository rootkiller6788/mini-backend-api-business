#include "priority_scheduler.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * L3 Engineering Structure:
 *   MLFQ: priority_levels[0..N-1], each with a FIFO queue.
 *   Fixed: single priority queue, sorted by priority then arrival time.
 *   Lottery: accumulated tickets array for O(log N) selection via binary search.
 *
 * L4 Theorem: Priority Inversion
 *   Classical example (Mars Pathfinder, 1997):
 *     - High-priority bus management task blocked on semaphore held by
 *       low-priority meteorology task
 *     - Medium-priority communications task preempted the low-priority one
 *     - Result: system reset every few minutes
 *     - Solution: priority inheritance (high priority inherited by holder)
 *
 * L5 Algorithm: MLFQ (Corbato et al., 1962 - CTSS)
 *   Rule 1: If Priority(A) > Priority(B), A runs before B
 *   Rule 2: If Priority(A) == Priority(B), round-robin (FIFO here)
 *   Rule 3: New jobs start at highest priority
 *   Rule 4: Jobs that use full quantum are demoted
 *   Rule 5: Aging: after S seconds, boost all jobs to top priority
 */

struct ps_scheduler_t {
    ps_job_entry_t   jobs[PS_MAX_JOBS];
    int              job_count;

    /* Per-level FIFO queue (indices into jobs[]) */
    int              queues[PS_MAX_PRIORITY_LEVELS][PS_MAX_JOBS_PER_LEVEL];
    int              queue_head[PS_MAX_PRIORITY_LEVELS];
    int              queue_tail[PS_MAX_PRIORITY_LEVELS];
    int              queue_count[PS_MAX_PRIORITY_LEVELS];

    ps_sched_algo_t  algorithm;
    ps_priority_level_t levels[PS_MAX_PRIORITY_LEVELS];
    int              level_count;
    int              aging_interval_sec;
    int              max_aging_boosts;

    time_t           last_aging;

    int              total_submitted;
    int              total_completed;
    double           total_wait_time;
    int              completed_for_avg;
};

static int job_find(ps_scheduler_t *sched, uint64_t id)
{
    int i;
    for (i = 0; i < sched->job_count; i++)
        if (sched->jobs[i].id == id && sched->jobs[i].active)
            return i;
    return -1;
}

static void enqueue_at_level(ps_scheduler_t *sched, int job_idx, int priority)
{
    if (priority < 0) priority = 0;
    if (priority >= sched->level_count) priority = sched->level_count - 1;

    int lev = priority;
    int q = sched->queue_tail[lev];
    sched->queues[lev][q] = job_idx;
    sched->queue_tail[lev] = (q + 1) % PS_MAX_JOBS_PER_LEVEL;
    sched->queue_count[lev]++;
}

static int dequeue_from_level(ps_scheduler_t *sched, int priority)
{
    if (priority < 0 || priority >= sched->level_count) return -1;
    int lev = priority;
    if (sched->queue_count[lev] == 0) return -1;

    int idx = sched->queues[lev][sched->queue_head[lev]];
    sched->queue_head[lev] = (sched->queue_head[lev] + 1)
                             % PS_MAX_JOBS_PER_LEVEL;
    sched->queue_count[lev]--;
    return idx;
}

ps_scheduler_t *ps_create(const ps_config_t *config)
{
    ps_scheduler_t *sched = (ps_scheduler_t *)calloc(1, sizeof(*sched));
    if (!sched) return NULL;

    if (config) {
        sched->algorithm         = config->algorithm;
        sched->level_count       = config->level_count > 0
                                   ? config->level_count : PS_MAX_PRIORITY_LEVELS;
        sched->aging_interval_sec = config->aging_interval_sec > 0
                                    ? config->aging_interval_sec : 30;
        sched->max_aging_boosts  = config->max_aging_boosts > 0
                                   ? config->max_aging_boosts : 3;
        if (sched->level_count > PS_MAX_PRIORITY_LEVELS)
            sched->level_count = PS_MAX_PRIORITY_LEVELS;
        memcpy(sched->levels, config->levels,
               sched->level_count * sizeof(ps_priority_level_t));
    } else {
        sched->algorithm         = PS_SCHED_MLFQ;
        sched->level_count       = 3;
        sched->aging_interval_sec = 30;
        sched->max_aging_boosts  = 3;

        sched->levels[0].priority  = 0; sched->levels[0].quantum_ms = 10;
        sched->levels[1].priority  = 1; sched->levels[1].quantum_ms = 50;
        sched->levels[2].priority  = 2; sched->levels[2].quantum_ms = 100;
    }

    sched->last_aging = time(NULL);
    return sched;
}

void ps_destroy(ps_scheduler_t *sched)
{
    if (sched) free(sched);
}

int ps_submit(ps_scheduler_t *sched, uint64_t id, ps_job_fn job,
              void *ud, int priority, int tickets)
{
    if (!sched || !job) return -1;
    if (sched->job_count >= PS_MAX_JOBS) return -2;
    if (priority < 0) priority = 0;
    if (priority >= sched->level_count) priority = sched->level_count - 1;

    ps_job_entry_t *entry = &sched->jobs[sched->job_count];
    memset(entry, 0, sizeof(*entry));
    entry->id                = id;
    entry->job               = job;
    entry->userdata          = ud;
    entry->priority          = priority;
    entry->original_priority = priority;
    entry->tickets           = tickets > 0 ? tickets : 1;
    entry->enqueued_at       = time(NULL);
    entry->active            = 1;

    int idx = sched->job_count++;

    enqueue_at_level(sched, idx, priority);
    sched->total_submitted++;
    return 0;
}

int ps_cancel(ps_scheduler_t *sched, uint64_t id)
{
    int idx = job_find(sched, id);
    if (idx < 0) return -1;
    sched->jobs[idx].active = 0;

    /* Mark completed since it was removed */
    sched->total_completed++;
    double waited = difftime(time(NULL), sched->jobs[idx].enqueued_at);
    if (waited > 0) {
        sched->total_wait_time += waited;
        sched->completed_for_avg++;
    }
    return 0;
}

int ps_boost_priority(ps_scheduler_t *sched, uint64_t id, int new_priority)
{
    int idx = job_find(sched, id);
    if (idx < 0) return -1;
    if (new_priority < 0 || new_priority >= sched->level_count) return -2;

    sched->jobs[idx].priority = new_priority;
    sched->jobs[idx].boost_count++;
    return 0;
}

int ps_restore_priority(ps_scheduler_t *sched, uint64_t id)
{
    int idx = job_find(sched, id);
    if (idx < 0) return -1;
    sched->jobs[idx].priority = sched->jobs[idx].original_priority;
    return 0;
}

/*
 * MLFQ schedule_next: pick highest-priority non-empty level.
 * O(P) where P = number of priority levels.
 */
static int schedule_mlfq(ps_scheduler_t *sched, ps_job_entry_t *out)
{
    int lev;
    for (lev = 0; lev < sched->level_count; lev++) {
        while (sched->queue_count[lev] > 0) {
            int idx = dequeue_from_level(sched, lev);
            if (idx < 0) break;
            if (sched->jobs[idx].active) {
                *out = sched->jobs[idx];

                /* Record wait time for statistics */
                double waited = difftime(time(NULL),
                                        sched->jobs[idx].enqueued_at);
                if (waited > 0) {
                    sched->total_wait_time += waited;
                    sched->completed_for_avg++;
                }

                sched->jobs[idx].active = 0;
                sched->total_completed++;
                return 1;
            }
            /* Skip inactive (cancelled) jobs, continue to next */
        }
    }
    return 0;
}

/* Count active jobs across all levels */
static int count_active(ps_scheduler_t *sched)
{
    int total = 0;
    int i;
    for (i = 0; i < sched->job_count; i++) {
        if (sched->jobs[i].active) total++;
    }
    return total;
}

/*
 * Fixed priority scheduler: pick the job at highest priority level.
 */
static int schedule_fixed(ps_scheduler_t *sched, ps_job_entry_t *out)
{
    return schedule_mlfq(sched, out); /* Same logic for fixed priority */
}

int ps_schedule_next(ps_scheduler_t *sched, ps_job_entry_t *out)
{
    if (!sched || !out) return 0;

    /* Auto-age jobs if interval has passed */
    time_t now = time(NULL);
    if (difftime(now, sched->last_aging) >= sched->aging_interval_sec) {
        ps_age_jobs(sched, now);
        sched->last_aging = now;
    }

    switch (sched->algorithm) {
        case PS_SCHED_MLFQ:
            return schedule_mlfq(sched, out);
        case PS_SCHED_FIXED:
            return schedule_fixed(sched, out);
        case PS_SCHED_LOTTERY: {
            unsigned int seed = (unsigned int)now;
            int idx = ps_lottery_select(sched, &seed);
            if (idx >= 0) {
                *out = sched->jobs[idx];
                sched->jobs[idx].active = 0;
                sched->total_completed++;
                return 1;
            }
            return 0;
        }
        default:
            return 0;
    }
}

int ps_schedule_next_timed(ps_scheduler_t *sched, ps_job_entry_t *out,
                            int timeout_ms)
{
    /* For simplicity, if no job available, return 0 immediately.
     * In a full implementation, this would block with timeout. */
    (void)timeout_ms;
    return ps_schedule_next(sched, out);
}

/*
 * L5 Algorithm: Priority Inheritance Protocol (Basic)
 *
 * When a high-priority job (waiter) is blocked waiting for a resource
 * held by a lower-priority job (holder), temporarily boost the holder's
 * priority to match the waiter's. This prevents a medium-priority job
 * from preempting the holder and causing unbounded priority inversion.
 *
 * O(1) operation.
 */
int ps_priority_inherit(ps_scheduler_t *sched, uint64_t holder_id,
                         uint64_t waiter_id)
{
    int hi = job_find(sched, holder_id);
    int wi = job_find(sched, waiter_id);
    if (hi < 0 || wi < 0) return -1;

    int waiter_pri = sched->jobs[wi].priority;
    int holder_pri = sched->jobs[hi].priority;

    /* Only boost if holder has lower priority (higher number) */
    if (holder_pri > waiter_pri) {
        sched->jobs[hi].priority = waiter_pri;
        return 1; /* priority was boosted */
    }
    return 0; /* no change needed */
}

/*
 * L5 Algorithm: Aging (Starvation Prevention)
 *
 * Periodically boosts the priority of long-waiting jobs to prevent
 * indefinite postponement. Each boost moves the job up one priority level.
 * After max_aging_boosts, the job reaches top priority and stays there.
 *
 * O(N) where N = number of active jobs.
 * Based on: Multics scheduler (Corbato & Vyssotsky, 1965)
 */
int ps_age_jobs(ps_scheduler_t *sched, time_t now)
{
    if (!sched) return -1;
    int aged = 0;
    int i;

    for (i = 0; i < sched->job_count; i++) {
        ps_job_entry_t *job = &sched->jobs[i];
        if (!job->active) continue;

        double waited = difftime(now, job->enqueued_at);
        if (waited >= sched->aging_interval_sec &&
            job->boost_count < sched->max_aging_boosts &&
            job->priority > 0) {

            job->priority--;
            job->boost_count++;
            job->enqueued_at = now; /* reset timer */
            aged++;
        }
    }
    return aged;
}

/*
 * L5 Algorithm: Lottery Scheduling (Waldspurger & Weihl, 1994)
 *
 * Each job has a number of tickets. A random number is drawn from
 * [0, total_tickets). The job whose ticket range contains the random
 * number wins the lottery.
 *
 * Properties:
 *   - Proportional share: a job with N% of tickets gets ~N% of CPU
 *   - Responsive: can dynamically adjust tickets
 *   - Fair over time: expected allocation = ticket_share
 *
 * O(N) implementation (can be optimized to O(log N) with prefix sums).
 */
int ps_lottery_select(ps_scheduler_t *sched, unsigned int *seed)
{
    if (!sched || !seed) return -1;

    int total_tickets = 0;
    int i;

    /* Compute total tickets */
    for (i = 0; i < sched->job_count; i++) {
        if (sched->jobs[i].active)
            total_tickets += sched->jobs[i].tickets;
    }

    if (total_tickets == 0) return -1;

    /* Draw a random ticket using LCG for portability */
    *seed = (*seed) * 1664525u + 1013904223u;
    int winner = (int)(((double)(*seed) / 4294967296.0) * (double)total_tickets);

    /* Find the winning job */
    int accumulator = 0;
    for (i = 0; i < sched->job_count; i++) {
        if (!sched->jobs[i].active) continue;
        accumulator += sched->jobs[i].tickets;
        if (winner < accumulator)
            return i;
    }

    /* Fallback: return last active job */
    for (i = sched->job_count - 1; i >= 0; i--)
        if (sched->jobs[i].active)
            return i;

    return -1;
}

int ps_queue_size(const ps_scheduler_t *sched)
{
    if (!sched) return 0;
    return count_active((ps_scheduler_t *)sched);
}

int ps_queue_size_at(const ps_scheduler_t *sched, int priority)
{
    if (!sched || priority < 0 || priority >= sched->level_count) return 0;
    /* Count active jobs at the given priority level in internal job array */
    int count = 0;
    int i;
    for (i = 0; i < sched->job_count; i++) {
        if (sched->jobs[i].active && sched->jobs[i].priority == priority)
            count++;
    }
    return count;
}

int ps_is_empty(const ps_scheduler_t *sched)
{
    return ps_queue_size(sched) == 0;
}

int ps_total_submitted(const ps_scheduler_t *sched)
{
    return sched ? sched->total_submitted : 0;
}

int ps_total_completed(const ps_scheduler_t *sched)
{
    return sched ? sched->total_completed : 0;
}

double ps_avg_wait_time(const ps_scheduler_t *sched)
{
    if (!sched || sched->completed_for_avg == 0) return 0;
    return sched->total_wait_time / (double)sched->completed_for_avg;
}
