#include "job_coordinator.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * L3 Engineering Structure: Facade pattern coordinating 9 subsystems.
 *
 * The coordinator:
 *   1. Receives job submissions with metadata (type, priority, etc.)
 *   2. Routes through appropriate protection layers (breaker, rate limiter)
 *   3. Dispatches to worker pool for execution
 *   4. Tracks progress and handles retries
 *   5. Provides unified health monitoring
 *
 * L6 Canonical Problem: This implements the core orchestration engine
 * common to all production job systems (Airflow, Temporal, Cadence, etc.)
 */

struct job_coordinator_t {
    /* Core subsystems */
    worker_pool_t       *pool;
    cron_scheduler_t    *cron;
    delayed_queue_t     *dq;
    task_retry_t        *retry;
    ps_scheduler_t *ps;

    /* Protection subsystems */
    circuit_breaker_t   *breakers[JC_MAX_BREAKERS];
    char                 breaker_names[JC_MAX_BREAKERS][32];
    int                  breaker_count;

    rate_limiter_t       rate_limiters[JC_MAX_RATE_LIMIT];
    char                 limit_names[JC_MAX_RATE_LIMIT][32];
    int                  limit_count;

    /* State */
    jc_state_t          state;
    time_t              started_at;
    int                 enable_breaker;
    int                 enable_limiter;
    int                 enable_dag;

    /* Statistics */
    jc_stats_t          stats;
    double              total_latency_ms;
    int                 latency_samples;
};

/* Default configuration */
static void apply_defaults(jc_config_t *cfg)
{
    if (cfg->max_workers <= 0)             cfg->max_workers = 4;
    if (cfg->queue_size <= 0)              cfg->queue_size = 256;
    if (cfg->graceful_timeout_ms <= 0)     cfg->graceful_timeout_ms = 10000;
    if (cfg->health_check_interval_ms <= 0) cfg->health_check_interval_ms = 5000;
}

job_coordinator_t *jc_create(const jc_config_t *config)
{
    job_coordinator_t *jc = (job_coordinator_t *)calloc(1, sizeof(*jc));
    if (!jc) return NULL;

    jc_config_t cfg;
    if (config) {
        cfg = *config;
        jc->enable_breaker = config->enable_circuit_breaker;
        jc->enable_limiter = config->enable_rate_limiter;
        jc->enable_dag     = config->enable_dag_scheduler;
    } else {
        memset(&cfg, 0, sizeof(cfg));
        jc->enable_breaker = 1;
        jc->enable_limiter = 1;
        jc->enable_dag     = 1;
    }
    apply_defaults(&cfg);

    /* Create worker pool */
    wp_config_t wcfg;
    memset(&wcfg, 0, sizeof(wcfg));
    wcfg.max_workers = cfg.max_workers;
    wcfg.queue_size  = cfg.queue_size;
    wcfg.graceful_shutdown_timeout_ms = cfg.graceful_timeout_ms;
    jc->pool = worker_pool_create(&wcfg);
    if (!jc->pool) { free(jc); return NULL; }

    /* Create cron scheduler */
    cron_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.max_jobs = 32;
    jc->cron = cron_scheduler_create(&ccfg);
    if (!jc->cron) { worker_pool_destroy(jc->pool); free(jc); return NULL; }

    /* Create delayed queue */
    jc->dq = delayed_queue_create(64, 1);
    if (!jc->dq) { cron_scheduler_destroy(jc->cron);
                   worker_pool_destroy(jc->pool); free(jc); return NULL; }

    /* Create retry system */
    jc->retry = task_retry_create(64);
    if (!jc->retry) { delayed_queue_destroy(jc->dq);
                      cron_scheduler_destroy(jc->cron);
                      worker_pool_destroy(jc->pool); free(jc); return NULL; }

    /* Create priority scheduler */
    jc->ps = ps_create(NULL);
    if (!jc->ps) { task_retry_destroy(jc->retry); delayed_queue_destroy(jc->dq);
                   cron_scheduler_destroy(jc->cron);
                   worker_pool_destroy(jc->pool); free(jc); return NULL; }

    jc->state     = JC_STATE_IDLE;
    jc->started_at = time(NULL);
    return jc;
}

void jc_destroy(job_coordinator_t *jc)
{
    if (!jc) return;
    jc_stop(jc, 0);

    wp_shutdown_graceful(jc->pool, 5000);
    worker_pool_destroy(jc->pool);

    cron_scheduler_destroy(jc->cron);
    delayed_queue_destroy(jc->dq);
    task_retry_destroy(jc->retry);
    ps_destroy(jc->ps);

    int i;
    for (i = 0; i < jc->breaker_count; i++)
        cb_destroy(jc->breakers[i]);

    free(jc);
}

/* ============ Internal: job execution wrapper ============ */

typedef struct {
    job_coordinator_t  *jc;
    jc_job_ctx_t        ctx;
    jc_job_fn           job_fn;
    jc_completion_fn    on_complete;
    void               *complete_ud;
    time_t              submitted_at;
} jc_wrapper_ctx_t;

static void jc_execute_wrapper(void *userdata)
{
    jc_wrapper_ctx_t *w = (jc_wrapper_ctx_t *)userdata;
    job_coordinator_t *jc = w->jc;

    time_t start = time(NULL);
    jc_result_t result = w->job_fn(&w->ctx);
    time_t end = time(NULL);

    /* Update stats */
    double latency = difftime(end, start) * 1000.0;
    jc->total_latency_ms += latency;
    jc->latency_samples++;

    switch (result) {
        case JC_RESULT_SUCCESS:
            jc->stats.total_completed++;
            break;
        case JC_RESULT_FAILED:
            jc->stats.total_failed++;
            break;
        case JC_RESULT_RETRY:
            jc->stats.total_retried++;
            break;
        case JC_RESULT_REJECTED:
            jc->stats.total_rejected++;
            break;
        case JC_RESULT_SKIPPED:
            jc->stats.total_skipped++;
            break;
        default: break;
    }

    if (w->on_complete)
        w->on_complete(&w->ctx, result, w->complete_ud);

    free(w);
}

/* ============ Job submission ============ */

int jc_submit_job(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                  jc_job_fn job_fn, jc_completion_fn on_complete,
                  void *complete_ud)
{
    if (!jc || !ctx || !job_fn) return -1;
    if (jc->state != JC_STATE_RUNNING) return -2;

    jc_wrapper_ctx_t *w = (jc_wrapper_ctx_t *)malloc(sizeof(*w));
    if (!w) return -3;

    w->jc           = jc;
    w->ctx          = *ctx;
    w->job_fn       = job_fn;
    w->on_complete  = on_complete;
    w->complete_ud  = complete_ud;
    w->submitted_at = time(NULL);

    int ret = wp_submit(jc->pool, ctx->job_id, jc_execute_wrapper, w,
                        NULL, NULL, ctx->job_type);
    if (ret < 0) {
        free(w);
        return ret;
    }

    jc->stats.total_submitted++;
    jc->stats.active_jobs++;
    return 0;
}

int jc_submit_with_breaker(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                            jc_job_fn job_fn, jc_completion_fn on_complete,
                            void *complete_ud, const char *breaker_name)
{
    if (!jc || !ctx || !job_fn) return -1;

    /* Look up circuit breaker */
    circuit_breaker_t *cb = NULL;
    int i;
    for (i = 0; i < jc->breaker_count; i++) {
        if (strcmp(jc->breaker_names[i], breaker_name) == 0) {
            cb = jc->breakers[i];
            break;
        }
    }

    if (cb && !cb_allow_request(cb)) {
        jc->stats.total_rejected++;
        if (on_complete)
            on_complete(ctx, JC_RESULT_REJECTED, complete_ud);
        return -2; /* rejected by circuit breaker */
    }

    return jc_submit_job(jc, ctx, job_fn, on_complete, complete_ud);
}

int jc_submit_rate_limited(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                            jc_job_fn job_fn, jc_completion_fn on_complete,
                            void *complete_ud, const char *limit_name,
                            double cost)
{
    if (!jc || !ctx || !job_fn) return -1;

    /* Look up rate limiter */
    rate_limiter_t *rl = NULL;
    int i;
    for (i = 0; i < jc->limit_count; i++) {
        if (strcmp(jc->limit_names[i], limit_name) == 0) {
            rl = &jc->rate_limiters[i];
            break;
        }
    }

    if (rl && !rl_consume(rl, cost)) {
        jc->stats.total_rejected++;
        if (on_complete)
            on_complete(ctx, JC_RESULT_REJECTED, complete_ud);
        return -2; /* rate limited */
    }

    return jc_submit_job(jc, ctx, job_fn, on_complete, complete_ud);
}

int jc_submit_dag(job_coordinator_t *jc, jdag_t *dag,
                  jc_job_fn *job_fns, int job_fn_count)
{
    if (!jc || !dag) return -1;
    (void)job_fns;
    (void)job_fn_count;

    /* Get ready nodes (indegree 0) */
    uint64_t ready[64];
    int ready_count = jdag_get_ready_nodes(dag, ready, 64);
    int submitted = 0;
    int i;

    for (i = 0; i < ready_count; i++) {
        jc_job_ctx_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.job_id = ready[i];
        ctx.job_name = "dag-job";
        ctx.job_type = "dag";

        /* Find corresponding job function */
        int node_idx = jdag_find_node(dag, ready[i]);
        jc_job_fn fn = (node_idx >= 0 && node_idx < job_fn_count)
                       ? job_fns[node_idx] : NULL;

        if (fn && jc_submit_job(jc, &ctx, fn, NULL, NULL) == 0)
            submitted++;
    }

    return submitted;
}

/* ============ Scheduling ============ */

int jc_schedule_cron(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                      jc_job_fn job_fn, const char *cron_spec, int catch_up)
{
    if (!jc || !ctx || !cron_spec) return -1;

    /* Wrap the job function */
    jc_wrapper_ctx_t *w = (jc_wrapper_ctx_t *)malloc(sizeof(*w));
    if (!w) return -2;
    w->jc         = jc;
    w->ctx        = *ctx;
    w->job_fn     = job_fn;
    w->on_complete = NULL;
    w->complete_ud = NULL;
    w->submitted_at = time(NULL);

    return cron_register_job(jc->cron, ctx->job_id, cron_spec,
                              (cron_job_fn)jc_execute_wrapper, w, catch_up);
}

int jc_schedule_delayed(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                         jc_job_fn job_fn, uint64_t delay_ms)
{
    if (!jc || !ctx) return -1;

    jc_wrapper_ctx_t *w = (jc_wrapper_ctx_t *)malloc(sizeof(*w));
    if (!w) return -2;
    w->jc         = jc;
    w->ctx        = *ctx;
    w->job_fn     = job_fn;
    w->on_complete = NULL;
    w->complete_ud = NULL;
    w->submitted_at = time(NULL);

    char key[64];
    snprintf(key, sizeof(key), "jc_delay_%llu", (unsigned long long)ctx->job_id);

    return dq_enqueue(jc->dq, ctx->job_id, key,
                      (dq_job_fn)jc_execute_wrapper, w, delay_ms);
}

int jc_submit_priority(job_coordinator_t *jc, const jc_job_ctx_t *ctx,
                        jc_job_fn job_fn, int priority)
{
    if (!jc || !ctx) return -1;

    jc_wrapper_ctx_t *w = (jc_wrapper_ctx_t *)malloc(sizeof(*w));
    if (!w) return -2;
    w->jc         = jc;
    w->ctx        = *ctx;
    w->job_fn     = job_fn;
    w->on_complete = NULL;
    w->complete_ud = NULL;
    w->submitted_at = time(NULL);

    return ps_submit(jc->ps, ctx->job_id, (ps_job_fn)jc_execute_wrapper,
                     w, priority, 1);
}

/* ============ Tick ============ */

int jc_tick(job_coordinator_t *jc)
{
    if (!jc || jc->state != JC_STATE_RUNNING) return -1;

    time_t now = time(NULL);
    int processed = 0;

    /* Process cron jobs */
    processed += cron_tick(jc->cron, now);

    /* Process delayed queue */
    dq_entry_t entry;
    while (dq_dequeue(jc->dq, now, &entry)) {
        if (entry.callback)
            entry.callback(entry.userdata);
        processed++;
    }

    /* Process retries */
    tr_process_dead_letters(jc->retry);

    /* Process priority scheduler */
    ps_job_entry_t ps_entry;
    if (ps_schedule_next(jc->ps, &ps_entry)) {
        if (ps_entry.job)
            ps_entry.job(ps_entry.userdata);
        processed++;
    }

    /* Update stats */
    jc->stats.queued_jobs   = wp_pending_tasks(jc->pool) + dq_size(jc->dq);
    jc->stats.active_jobs   = wp_busy_workers(jc->pool);
    jc->stats.uptime_seconds = difftime(now, jc->started_at);

    return processed;
}

/* ============ Lifecycle ============ */

int jc_start(job_coordinator_t *jc)
{
    if (!jc) return -1;
    jc->state = JC_STATE_RUNNING;
    jc->started_at = time(NULL);
    return 0;
}

int jc_stop(job_coordinator_t *jc, int graceful)
{
    if (!jc) return -1;
    jc->state = JC_STATE_STOPPED;
    if (graceful)
        wp_shutdown_graceful(jc->pool, 5000);
    else
        wp_shutdown(jc->pool);
    return 0;
}

int jc_drain(job_coordinator_t *jc, int timeout_ms)
{
    if (!jc) return -1;
    jc->state = JC_STATE_DRAINING;

    int waited = 0;
    while (wp_pending_tasks(jc->pool) > 0 && waited < timeout_ms) {
        jc_tick(jc);
        /* Sleep 100ms */
#ifdef _WIN32
        Sleep(100);
#else
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000L;
        nanosleep(&ts, NULL);
#endif
        waited += 100;
    }

    jc->state = JC_STATE_STOPPED;
    return 0;
}

jc_state_t jc_get_state(const job_coordinator_t *jc)
{
    return jc ? jc->state : JC_STATE_STOPPED;
}

/* ============ Circuit Breaker Management ============ */

int jc_register_breaker(job_coordinator_t *jc, const char *name)
{
    if (!jc || !name || jc->breaker_count >= JC_MAX_BREAKERS) return -1;

    cb_config_t cbc;
    memset(&cbc, 0, sizeof(cbc));
    cbc.failure_threshold    = 5;
    cbc.success_threshold    = 2;
    cbc.cooldown_ms          = 30000;
    cbc.half_open_max_requests = 1;

    jc->breakers[jc->breaker_count] = cb_create(&cbc, name);
    if (!jc->breakers[jc->breaker_count]) return -2;

    strncpy(jc->breaker_names[jc->breaker_count], name, 31);
    jc->breaker_names[jc->breaker_count][31] = '\0';
    jc->breaker_count++;
    return 0;
}

int jc_get_breaker(const job_coordinator_t *jc, const char *name,
                   circuit_breaker_t **out)
{
    if (!jc || !name || !out) return -1;
    int i;
    for (i = 0; i < jc->breaker_count; i++) {
        if (strcmp(jc->breaker_names[i], name) == 0) {
            *out = jc->breakers[i];
            return 0;
        }
    }
    return -1;
}

/* ============ Rate Limiter Management ============ */

int jc_register_rate_limit(job_coordinator_t *jc, const char *name,
                            rl_type_t type, double rate, double burst)
{
    if (!jc || !name || jc->limit_count >= JC_MAX_RATE_LIMIT) return -1;

    strncpy(jc->limit_names[jc->limit_count], name, 31);
    jc->limit_names[jc->limit_count][31] = '\0';

    switch (type) {
        case RL_TYPE_TOKEN_BUCKET:
            rl_token_bucket_init(&jc->rate_limiters[jc->limit_count], rate, burst);
            break;
        case RL_TYPE_LEAKY_BUCKET:
            rl_leaky_bucket_init(&jc->rate_limiters[jc->limit_count], rate, burst);
            break;
        case RL_TYPE_SLIDING_WINDOW:
            rl_sliding_window_init(&jc->rate_limiters[jc->limit_count], rate, burst);
            break;
        case RL_TYPE_FIXED_WINDOW:
            rl_fixed_window_init(&jc->rate_limiters[jc->limit_count], rate, burst);
            break;
        default: return -2;
    }

    jc->limit_count++;
    return 0;
}

int jc_get_rate_limit(const job_coordinator_t *jc, const char *name,
                      rate_limiter_t **out)
{
    if (!jc || !name || !out) return -1;
    int i;
    for (i = 0; i < jc->limit_count; i++) {
        if (strcmp(jc->limit_names[i], name) == 0) {
            *out = (rate_limiter_t *)&jc->rate_limiters[i];
            return 0;
        }
    }
    return -1;
}

/* ============ Statistics ============ */

int jc_get_stats(const job_coordinator_t *jc, jc_stats_t *out)
{
    if (!jc || !out) return -1;
    *out = jc->stats;

    out->uptime_seconds = difftime(time(NULL), jc->started_at);
    if (out->uptime_seconds > 0) {
        out->completion_rate =
            (double)out->total_completed / out->uptime_seconds;
    }
    if (out->total_completed + out->total_failed > 0) {
        out->failure_rate =
            (double)out->total_failed /
            (double)(out->total_completed + out->total_failed);
    }
    if (jc->latency_samples > 0) {
        out->avg_latency_ms =
            jc->total_latency_ms / (double)jc->latency_samples;
    }

    return 0;
}

void jc_reset_stats(job_coordinator_t *jc)
{
    if (!jc) return;
    memset(&jc->stats, 0, sizeof(jc->stats));
    jc->total_latency_ms = 0;
    jc->latency_samples  = 0;
}

int jc_health_check(const job_coordinator_t *jc)
{
    if (!jc) return 0;
    return (jc->state == JC_STATE_RUNNING ||
            jc->state == JC_STATE_DRAINING) &&
           wp_is_running(jc->pool);
}
