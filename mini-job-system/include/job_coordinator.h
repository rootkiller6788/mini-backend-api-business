#ifndef JOB_COORDINATOR_H
#define JOB_COORDINATOR_H

#include <stdint.h>
#include <time.h>

#include "cron_scheduler.h"
#include "delayed_queue.h"
#include "worker_pool.h"
#include "task_retry.h"
#include "job_progress.h"
#include "job_dag.h"
#include "rate_limiter.h"
#include "circuit_breaker.h"
#include "priority_scheduler.h"

/*
 * L2 Core Concept: Job Coordinator — unified orchestration layer that
 * integrates all scheduling subsystems into a coherent whole.
 *
 * L3 Engineering Structure: Facade pattern over 9 subsystems:
 *   - cron_scheduler: periodic time-based triggers
 *   - delayed_queue: time-delayed execution
 *   - worker_pool: concurrent execution with resource limits
 *   - task_retry: failure resilience
 *   - job_progress: observability
 *   - job_dag: dependency ordering
 *   - rate_limiter: flow control
 *   - circuit_breaker: fault isolation
 *   - priority_scheduler: fairness and urgency
 *
 * L6 Canonical Problem: Distributed job orchestration platform
 *   analogous to: Apache Airflow, Temporal.io, AWS Step Functions,
 *   Google Cloud Scheduler.
 *
 * L7 Applications:
 *   1. ETL pipeline orchestration (data engineering)
 *   2. Cron-based batch processing (financial reconciliation)
 *   3. Event-driven workflow engine (microservices)
 */

#define JC_MAX_BREAKERS    16
#define JC_MAX_RATE_LIMIT  16

/* Overall coordinator state */
typedef enum {
    JC_STATE_IDLE      = 0,
    JC_STATE_RUNNING   = 1,
    JC_STATE_DRAINING  = 2,
    JC_STATE_STOPPED   = 3
} jc_state_t;

/* Job result codes */
typedef enum {
    JC_RESULT_SUCCESS   = 0,
    JC_RESULT_FAILED    = 1,
    JC_RESULT_RETRY     = 2,
    JC_RESULT_REJECTED  = 3,
    JC_RESULT_SKIPPED   = 4,
    JC_RESULT_TIMEOUT   = 5
} jc_result_t;

/* Generic job context passed to callbacks */
typedef struct {
    uint64_t   job_id;
    const char *job_name;
    const char *job_type;
    int         priority;
    void       *userdata;
} jc_job_ctx_t;

/* Job execution callback (returns result code) */
typedef jc_result_t (*jc_job_fn)(const jc_job_ctx_t *ctx);

/* Job completion callback */
typedef void (*jc_completion_fn)(const jc_job_ctx_t *ctx, jc_result_t result,
                                  void *userdata);

/* Coordinator opaque type */
typedef struct job_coordinator_t job_coordinator_t;

/* Coordinator configuration */
typedef struct {
    int   max_workers;
    int   queue_size;
    int   graceful_timeout_ms;
    int   health_check_interval_ms;
    int   enable_circuit_breaker;
    int   enable_rate_limiter;
    int   enable_dag_scheduler;
} jc_config_t;

/* Statistics */
typedef struct {
    uint64_t  total_submitted;
    uint64_t  total_completed;
    uint64_t  total_failed;
    uint64_t  total_retried;
    uint64_t  total_rejected;
    uint64_t  total_skipped;
    double    uptime_seconds;
    int       active_jobs;
    int       queued_jobs;
    double    completion_rate;    /* jobs per second */
    double    failure_rate;       /* 0.0 - 1.0 */
    double    avg_latency_ms;
} jc_stats_t;

/* Lifecycle */
job_coordinator_t *jc_create(const jc_config_t *config);
void               jc_destroy(job_coordinator_t *jc);

/* Job submission with full options */
int  jc_submit_job(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                   jc_job_fn job_fn, jc_completion_fn on_complete,
                   void *complete_ud);

/* Submit a job with circuit breaker protection */
int  jc_submit_with_breaker(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                             jc_job_fn job_fn, jc_completion_fn on_complete,
                             void *complete_ud, const char *breaker_name);

/* Submit a rate-limited job */
int  jc_submit_rate_limited(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                             jc_job_fn job_fn, jc_completion_fn on_complete,
                             void *complete_ud, const char *limit_name,
                             double cost);

/* Submit a DAG of jobs (all at once, coordinator handles ordering) */
int  jc_submit_dag(job_coordinator_t *jc, jdag_t *dag,
                   jc_job_fn *job_fns, int job_fn_count);

/* Schedule a recurring cron job through the coordinator */
int  jc_schedule_cron(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                      jc_job_fn job_fn, const char *cron_spec,
                      int catch_up);

/* Enqueue a delayed job */
int  jc_schedule_delayed(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                          jc_job_fn job_fn, uint64_t delay_ms);

/* Priority scheduling */
int  jc_submit_priority(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                         jc_job_fn job_fn, int priority);

/* Tick the coordinator (process cron, delayed queue, retries) */
int  jc_tick(job_coordinator_t *jc);

/* Start/stop */
int  jc_start(job_coordinator_t *jc);
int  jc_stop(job_coordinator_t *jc, int graceful);
int  jc_drain(job_coordinator_t *jc, int timeout_ms);

/* State */
jc_state_t jc_get_state(const job_coordinator_t *jc);

/* Circuit breaker management */
int  jc_register_breaker(job_coordinator_t *jc, const char *name);
int  jc_get_breaker(const job_coordinator_t *jc, const char *name,
                    circuit_breaker_t **out);

/* Rate limiter management */
int  jc_register_rate_limit(job_coordinator_t *jc, const char *name,
                             rl_type_t type, double rate, double burst);
int  jc_get_rate_limit(const job_coordinator_t *jc, const char *name,
                       rate_limiter_t **out);

/* Statistics */
int  jc_get_stats(const job_coordinator_t *jc, jc_stats_t *out);
void jc_reset_stats(job_coordinator_t *jc);

/* Health check: returns non-zero if coordinator is healthy */
int  jc_health_check(const job_coordinator_t *jc);

#endif
