#include "worker_pool.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define WP_WORKER_ALIVE    0
#define WP_WORKER_IDLE     1
#define WP_WORKER_BUSY     2
#define WP_WORKER_EXIT     3

typedef struct {
    int      id;
    int      state;
    time_t   last_heartbeat;
    wp_task_t current_task;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
} wp_worker_t;

struct worker_pool_t {
    wp_worker_t             workers[WP_MAX_WORKERS];
    int                     worker_count;
    int                     max_workers;
    wp_task_queue_t         queue;
    volatile int            running;
    wp_heartbeat_fn         heartbeat_cb;
    int                     heartbeat_interval;
    wp_concurrency_limit_t  concurrency[WP_MAX_JOB_TYPES];
    char                    job_type_names[WP_MAX_JOB_TYPES][32];
    int                     job_type_count;
    int                     graceful_timeout_ms;
    volatile int            shutting_down;
};

static int q_init(wp_task_queue_t *q, int capacity)
{
    q->tasks    = (wp_task_t *)calloc(capacity, sizeof(wp_task_t));
    q->capacity = capacity;
    q->head     = 0;
    q->tail     = 0;
    q->size     = 0;
    if (!q->tasks) return -1;
#ifdef _WIN32
    InitializeCriticalSection(&q->lock);
    InitializeConditionVariable(&q->cond);
#else
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
#endif
    return 0;
}

static void q_destroy(wp_task_queue_t *q)
{
    free(q->tasks);
#ifdef _WIN32
    DeleteCriticalSection(&q->lock);
#else
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->cond);
#endif
}

static int q_push(wp_task_queue_t *q, const wp_task_t *task)
{
#ifdef _WIN32
    EnterCriticalSection(&q->lock);
#else
    pthread_mutex_lock(&q->lock);
#endif
    if (q->size >= q->capacity) {
#ifdef _WIN32
        LeaveCriticalSection(&q->lock);
#else
        pthread_mutex_unlock(&q->lock);
#endif
        return -1;
    }
    q->tasks[q->tail] = *task;
    q->tail = (q->tail + 1) % q->capacity;
    q->size++;
#ifdef _WIN32
    WakeConditionVariable(&q->cond);
    LeaveCriticalSection(&q->lock);
#else
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
#endif
    return 0;
}

static int q_pop(wp_task_queue_t *q, wp_task_t *out, int timeout_ms)
{
#ifdef _WIN32
    EnterCriticalSection(&q->lock);
    while (q->size == 0) {
        if (!SleepConditionVariableCS(&q->cond, &q->lock,
              timeout_ms > 0 ? (DWORD)timeout_ms : INFINITE)) {
            LeaveCriticalSection(&q->lock);
            return 0;
        }
    }
    *out = q->tasks[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    LeaveCriticalSection(&q->lock);
    return 1;
#else
    pthread_mutex_lock(&q->lock);
    if (timeout_ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (q->size == 0) {
            if (pthread_cond_timedwait(&q->cond, &q->lock, &ts) != 0) {
                pthread_mutex_unlock(&q->lock);
                return 0;
            }
        }
    } else {
        while (q->size == 0)
            pthread_cond_wait(&q->cond, &q->lock);
    }
    *out = q->tasks[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    pthread_mutex_unlock(&q->lock);
    return 1;
#endif
}

static int find_concurrency_index(worker_pool_t *pool, const char *job_type)
{
    int i;
    if (!job_type) return -1;
    for (i = 0; i < pool->job_type_count; i++) {
        if (strcmp(pool->job_type_names[i], job_type) == 0)
            return i;
    }
    if (pool->job_type_count < WP_MAX_JOB_TYPES) {
        i = pool->job_type_count++;
        strncpy(pool->job_type_names[i], job_type, 31);
        pool->job_type_names[i][31] = '\0';
        pool->concurrency[i].max_concurrent = 0;
        pool->concurrency[i].current = 0;
        return i;
    }
    return -1;
}

#ifdef _WIN32
static DWORD WINAPI worker_thread(LPVOID arg)
#else
static void *worker_thread(void *arg)
#endif
{
    worker_pool_t *pool   = (worker_pool_t *)arg;
    int worker_id = -1;
    int i;

    for (i = 0; i < pool->max_workers; i++) {
        if (pool->workers[i].state == WP_WORKER_ALIVE) {
            worker_id = i;
            pool->workers[i].state = WP_WORKER_IDLE;
            break;
        }
    }

    while (pool->running && !pool->shutting_down) {
        wp_task_t task;
        pool->workers[worker_id].state = WP_WORKER_IDLE;
        pool->workers[worker_id].last_heartbeat = time(NULL);

        if (!q_pop(&pool->queue, &task, 1000))
            continue;

        int ci = -1;
        if (task.job_type)
            ci = find_concurrency_index(pool, task.job_type);

        while (ci >= 0) {
#ifdef _WIN32
            EnterCriticalSection(&pool->queue.lock);
#else
            pthread_mutex_lock(&pool->queue.lock);
#endif
            if (pool->concurrency[ci].max_concurrent == 0 ||
                pool->concurrency[ci].current < pool->concurrency[ci].max_concurrent) {
                pool->concurrency[ci].current++;
#ifdef _WIN32
                LeaveCriticalSection(&pool->queue.lock);
#else
                pthread_mutex_unlock(&pool->queue.lock);
#endif
                break;
            }
#ifdef _WIN32
            LeaveCriticalSection(&pool->queue.lock);
#else
            pthread_mutex_unlock(&pool->queue.lock);
#endif
            Sleep(10);
        }

        pool->workers[worker_id].state = WP_WORKER_BUSY;
        pool->workers[worker_id].current_task = task;
        pool->workers[worker_id].last_heartbeat = time(NULL);

        task.task(task.userdata);

        if (task.ack)
            task.ack(task.job_id, 0, task.ack_ud);

        if (ci >= 0 && task.job_type) {
#ifdef _WIN32
            EnterCriticalSection(&pool->queue.lock);
            pool->concurrency[ci].current--;
            LeaveCriticalSection(&pool->queue.lock);
#else
            pthread_mutex_lock(&pool->queue.lock);
            pool->concurrency[ci].current--;
            pthread_mutex_unlock(&pool->queue.lock);
#endif
        }
    }
    pool->workers[worker_id].state = WP_WORKER_EXIT;
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

worker_pool_t *worker_pool_create(const wp_config_t *config)
{
    worker_pool_t *pool = (worker_pool_t *)calloc(1, sizeof(*pool));
    if (!pool) return NULL;

    pool->max_workers = (config && config->max_workers > 0)
                        ? config->max_workers : 4;
    if (pool->max_workers > WP_MAX_WORKERS)
        pool->max_workers = WP_MAX_WORKERS;

    int qs = (config && config->queue_size > 0) ? config->queue_size
             : WP_DEFAULT_QUEUE_SIZE;
    if (q_init(&pool->queue, qs) < 0) { free(pool); return NULL; }

    pool->graceful_timeout_ms = (config && config->graceful_shutdown_timeout_ms > 0)
                                ? config->graceful_shutdown_timeout_ms : 10000;
    pool->running      = 1;
    pool->shutting_down = 0;
    pool->job_type_count = 0;

    int i;
    for (i = 0; i < pool->max_workers; i++) {
        pool->workers[i].id    = i;
        pool->workers[i].state = WP_WORKER_EXIT;
#ifdef _WIN32
        pool->workers[i].thread = CreateThread(NULL, 0, worker_thread,
                                                pool, 0, NULL);
        if (pool->workers[i].thread) {
            pool->workers[i].state = WP_WORKER_ALIVE;
            pool->worker_count++;
        }
#else
        pool->workers[i].state = WP_WORKER_ALIVE;
        pool->worker_count++;
        pthread_create(&pool->workers[i].thread, NULL,
                       worker_thread, pool);
#endif
    }
    return pool;
}

void worker_pool_destroy(worker_pool_t *pool)
{
    if (!pool) return;
    wp_shutdown(pool);

    int i;
#ifdef _WIN32
    for (i = 0; i < pool->worker_count; i++) {
        if (pool->workers[i].thread) {
            WaitForSingleObject(pool->workers[i].thread, INFINITE);
            CloseHandle(pool->workers[i].thread);
        }
    }
#else
    for (i = 0; i < pool->worker_count; i++)
        pthread_join(pool->workers[i].thread, NULL);
#endif
    q_destroy(&pool->queue);
    free(pool);
}

int wp_submit(worker_pool_t *pool, uint64_t job_id, wp_task_fn task,
              void *ud, wp_ack_fn ack, void *ack_ud, const char *job_type)
{
    if (!pool || !task) return -1;
    wp_task_t t;
    memset(&t, 0, sizeof(t));
    t.job_id    = job_id;
    t.task      = task;
    t.userdata  = ud;
    t.ack       = ack;
    t.ack_ud    = ack_ud;
    t.job_type  = job_type;
    return q_push(&pool->queue, &t);
}

int wp_submit_batch(worker_pool_t *pool, const wp_task_t *tasks, int count)
{
    int i, ok = 0;
    if (!pool || !tasks) return -1;
    for (i = 0; i < count; i++)
        if (q_push(&pool->queue, &tasks[i]) == 0) ok++;
    return ok;
}

void wp_shutdown(worker_pool_t *pool)
{
    if (!pool) return;
    pool->running = 0;
    pool->shutting_down = 1;
}

void wp_shutdown_graceful(worker_pool_t *pool, int timeout_ms)
{
    int waited = 0;
    if (!pool) return;
    pool->shutting_down = 1;

    while (wp_busy_workers(pool) > 0 && waited < timeout_ms) {
        Sleep(100);
        waited += 100;
    }
    pool->running = 0;
}

int wp_set_concurrency_limit(worker_pool_t *pool, const char *job_type,
                             int max_concurrent)
{
    int idx = find_concurrency_index(pool, job_type);
    if (idx < 0) return -1;
    pool->concurrency[idx].max_concurrent = max_concurrent;
    return 0;
}

int wp_busy_workers(const worker_pool_t *pool)
{
    int i, busy = 0;
    if (!pool) return 0;
    for (i = 0; i < pool->worker_count; i++)
        if (pool->workers[i].state == WP_WORKER_BUSY) busy++;
    return busy;
}

int wp_pending_tasks(const worker_pool_t *pool)
{
    if (!pool) return 0;
    return pool->queue.size;
}

int wp_is_running(const worker_pool_t *pool)
{
    return pool ? pool->running : 0;
}

void wp_set_heartbeat_callback(worker_pool_t *pool, wp_heartbeat_fn hb,
                               int interval_sec)
{
    if (!pool) return;
    pool->heartbeat_cb       = hb;
    pool->heartbeat_interval = interval_sec;
}
