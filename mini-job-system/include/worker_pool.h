#ifndef WORKER_POOL_H
#define WORKER_POOL_H

#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#define WP_MAX_WORKERS         64
#define WP_MAX_JOB_TYPES       32
#define WP_HEARTBEAT_INTERVAL  5
#define WP_DEFAULT_QUEUE_SIZE  256

typedef void (*wp_task_fn)(void *userdata);
typedef void (*wp_ack_fn)(uint64_t job_id, int status, void *userdata);
typedef void (*wp_heartbeat_fn)(int worker_id, time_t last_seen);

typedef struct {
    uint64_t     job_id;
    wp_task_fn   task;
    void        *userdata;
    wp_ack_fn    ack;
    void        *ack_ud;
    const char  *job_type;
} wp_task_t;

typedef struct {
    wp_task_t *tasks;
    int        head;
    int        tail;
    int        size;
    int        capacity;
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cond;
#else
    pthread_mutex_t lock;
    pthread_cond_t  cond;
#endif
} wp_task_queue_t;

typedef struct {
    int  max_concurrent;
    int  current;
} wp_concurrency_limit_t;

typedef struct {
    int  max_workers;
    int  queue_size;
    int  graceful_shutdown_timeout_ms;
    void *reserved;
} wp_config_t;

typedef struct worker_pool_t worker_pool_t;

worker_pool_t *worker_pool_create(const wp_config_t *config);
void           worker_pool_destroy(worker_pool_t *pool);

int  wp_submit(worker_pool_t *pool, uint64_t job_id, wp_task_fn task,
               void *ud, wp_ack_fn ack, void *ack_ud, const char *job_type);
int  wp_submit_batch(worker_pool_t *pool, const wp_task_t *tasks, int count);

void wp_shutdown(worker_pool_t *pool);
void wp_shutdown_graceful(worker_pool_t *pool, int timeout_ms);

int  wp_set_concurrency_limit(worker_pool_t *pool, const char *job_type, int max_concurrent);

int  wp_busy_workers(const worker_pool_t *pool);
int  wp_pending_tasks(const worker_pool_t *pool);
int  wp_is_running(const worker_pool_t *pool);

void wp_set_heartbeat_callback(worker_pool_t *pool, wp_heartbeat_fn hb, int interval_sec);

#endif
