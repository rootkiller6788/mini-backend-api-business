# mini-job-system API Reference

## Module Overview

| Module | Header | Description |
|--------|--------|-------------|
| Cron Scheduler | `cron_scheduler.h` | Parse cron expressions, schedule recurring jobs |
| Delayed Queue | `delayed_queue.h` | Enqueue jobs with delay, min-heap priority queue |
| Worker Pool | `worker_pool.h` | Fixed-size thread pool with concurrency control |
| Task Retry | `task_retry.h` | Exponential backoff, retry logic, dead letter queue |
| Job Progress | `job_progress.h` | Progress tracking, state machine, ETA estimation |

---

## Cron Scheduler (`cron_scheduler.h`)

### Types

```c
typedef enum {
    CRON_FIELD_SECOND,
    CRON_FIELD_MINUTE,
    CRON_FIELD_HOUR,
    CRON_FIELD_DAY,
    CRON_FIELD_MONTH,
    CRON_FIELD_WEEKDAY
} cron_field_t;

typedef void (*cron_job_fn)(void *userdata);
```

### API

| Function | Description |
|----------|-------------|
| `cron_scheduler_create(config)` | Create a cron scheduler |
| `cron_scheduler_destroy(sched)` | Destroy and free the scheduler |
| `cron_parse_expression(expr, spec)` | Parse a 5 or 6-field cron expression |
| `cron_parse_field(field, str, min, max)` | Parse a single cron field |
| `cron_register_job(sched, id, spec, cb, ud, catch_up)` | Register a cron job |
| `cron_unregister_job(sched, id)` | Remove a cron job by ID |
| `cron_activate_job(sched, id, active)` | Activate/deactivate a job |
| `cron_next_fire_time(expr, from)` | Calculate next fire time after `from` |
| `cron_matches(expr, tm)` | Check if a time matches a cron expression |
| `cron_tick(sched, now)` | Fire all due jobs (respects catch-up) |
| `cron_run_pending(sched)` | Convenience: tick with current time |

### Cron Expression Format

```
sec min hour day month weekday
```

Or omit seconds (5-field):
```
min hour day month weekday
```

- `*` — any value (wildcard)
- `1-5` — range (1 through 5)
- `*/5` — step (every 5 units)
- `1,3,5` — specific values
- Combined: `1-5,10,15/2`

---

## Delayed Queue (`delayed_queue.h`)

### Types

```c
typedef enum {
    DQ_LEVEL_1S    = 1000,
    DQ_LEVEL_10S   = 10000,
    DQ_LEVEL_1MIN  = 60000,
    DQ_LEVEL_10MIN = 600000,
    DQ_LEVEL_1HR   = 3600000
} dq_delay_level_t;

typedef void (*dq_job_fn)(void *userdata);
```

### API

| Function | Description |
|----------|-------------|
| `delayed_queue_create(capacity, dedup)` | Create a delayed queue |
| `delayed_queue_destroy(dq)` | Destroy and free the queue |
| `dq_enqueue(dq, id, key, cb, ud, delay_ms)` | Enqueue with custom delay |
| `dq_enqueue_level(dq, id, key, cb, ud, level)` | Enqueue using a delay level |
| `dq_cancel(dq, id)` | Cancel by job ID |
| `dq_cancel_by_key(dq, key)` | Cancel by unique key |
| `dq_dequeue(dq, now, out)` | Dequeue ready jobs (fire_time <= now) |
| `dq_peek(dq, out)` | Peek at earliest job |
| `dq_is_empty(dq)` | Check if queue is empty |
| `dq_size(dq)` | Get queue size |
| `dq_next_delay_ms(dq, now)` | Milliseconds until next job fires |

### Delay Levels

| Level | Value | Use Case |
|-------|-------|----------|
| `DQ_LEVEL_1S` | 1 second | Quick debounce, rate limit reset |
| `DQ_LEVEL_10S` | 10 seconds | Periodic cleanup, heartbeat |
| `DQ_LEVEL_1MIN` | 1 minute | Cache refresh, metrics flush |
| `DQ_LEVEL_10MIN` | 10 minutes | Batch processing, reconciliation |
| `DQ_LEVEL_1HR` | 1 hour | Daily reports, data archival |

---

## Worker Pool (`worker_pool.h`)

### Types

```c
typedef void (*wp_task_fn)(void *userdata);
typedef void (*wp_ack_fn)(uint64_t job_id, int status, void *userdata);
typedef void (*wp_heartbeat_fn)(int worker_id, time_t last_seen);
```

### API

| Function | Description |
|----------|-------------|
| `worker_pool_create(config)` | Create a worker pool |
| `worker_pool_destroy(pool)` | Destroy pool (waits for threads) |
| `wp_submit(pool, id, task, ud, ack, ack_ud, type)` | Submit a single task |
| `wp_submit_batch(pool, tasks, count)` | Submit batch of tasks |
| `wp_shutdown(pool)` | Signal shutdown |
| `wp_shutdown_graceful(pool, timeout_ms)` | Wait for running tasks, then shutdown |
| `wp_set_concurrency_limit(pool, type, max)` | Limit concurrent tasks by type |
| `wp_busy_workers(pool)` | Number of currently busy workers |
| `wp_pending_tasks(pool)` | Number of tasks waiting in queue |
| `wp_is_running(pool)` | Check if pool is active |
| `wp_set_heartbeat_callback(pool, cb, interval)` | Set heartbeat monitor |

### Leader/Follower Pattern

Workers pull tasks from a shared queue. They run a polling loop that:
1. Waits for a task on the condition variable (follower)
2. Grabs the task and marks itself busy (leader)
3. Executes the task
4. Calls the ack callback
5. Returns to follower state

---

## Task Retry (`task_retry.h`)

### Types

```c
typedef enum {
    TR_ERR_NONE         = 0,
    TR_ERR_TIMEOUT      = 1,
    TR_ERR_HTTP_503     = 2,
    TR_ERR_CONN_REFUSED = 3,
    TR_ERR_TEMP_FAILURE = 4,
    TR_ERR_UNKNOWN      = 99
} tr_error_code_t;

typedef void (*tr_task_fn)(void *userdata);
typedef void (*tr_dead_letter_fn)(uint64_t id, tr_error_code_t err, void *ud);
```

### API

| Function | Description |
|----------|-------------|
| `task_retry_create(capacity)` | Create a retry manager |
| `task_retry_destroy(tr)` | Destroy and free |
| `tr_register(tr, id, task, ud, max, base_ms)` | Register task with retry config |
| `tr_set_retry_on(tr, id, codes, count)` | Set which errors trigger retry |
| `tr_set_dead_letter(tr, id, cb, ud)` | Set dead letter callback |
| `tr_report_result(tr, id, err)` | Report task execution result |
| `tr_can_retry(tr, id, now)` | Check if retry is available now |
| `tr_trigger_retry(tr, id, now)` | Execute retry if due |
| `tr_get_retry_count(tr, id)` | Get retry attempts count |
| `tr_is_exhausted(tr, id)` | Check if max retries reached |
| `tr_calc_delay(count, base, jitter, seed)` | Calculate backoff delay |
| `tr_process_dead_letters(tr)` | Process all dead-lettered jobs |

### Backoff Formula

```
delay = base_ms * 2^retry_count
capped at 1 hour (3600000 ms)

With jitter: delay += rand(-50ms, +50ms)
```

### Retry Sequence Example

| Attempt | Delay (base=100ms) | With Jitter |
|---------|---------------------|-------------|
| 0 (first) | 100ms | ~50–150ms |
| 1 | 200ms | ~150–250ms |
| 2 | 400ms | ~350–450ms |
| 3 | 800ms | ~750–850ms |
| 4 | 1600ms | ~1550–1650ms |
| ... | ... | ... |
| cap | 3600000ms (1hr) | ~3599950–3600050ms |

---

## Job Progress (`job_progress.h`)

### State Machine

```
QUEUED -> RUNNING -> SUCCEEDED
                  -> FAILED
                  -> CANCELLED
```

### Types

```c
typedef enum {
    JP_STATE_QUEUED    = 0,
    JP_STATE_RUNNING   = 1,
    JP_STATE_SUCCEEDED = 2,
    JP_STATE_FAILED    = 3,
    JP_STATE_CANCELLED = 4
} jp_state_t;

typedef void (*jp_progress_cb)(uint64_t id, int done, int total,
                                double pct, void *ud);
typedef void (*jp_state_cb)(uint64_t id, jp_state_t old,
                             jp_state_t new, void *ud);
```

### API

| Function | Description |
|----------|-------------|
| `job_progress_create()` | Allocate progress tracker |
| `job_progress_destroy(jp)` | Free tracker |
| `jp_set_total_steps(jp, total)` | Set total step count |
| `jp_set_progress_callback(jp, cb, ud)` | Register progress update callback |
| `jp_set_state_callback(jp, cb, ud)` | Register state transition callback |
| `jp_transition_state(jp, new, msg)` | Change job state |
| `jp_update_progress(jp, completed, msg)` | Set completed steps count |
| `jp_increment_progress(jp, msg)` | Increment completed by 1 |
| `jp_fail(jp, msg)` | Mark job as failed |
| `jp_cancel(jp, msg)` | Mark job as cancelled |
| `jp_get_state(jp)` | Get current state |
| `jp_get_progress(jp)` | Get progress percentage (0.0–100.0) |
| `jp_get_eta(jp)` | Get estimated seconds remaining |
| `jp_get_elapsed(jp)` | Get elapsed seconds |
| `jp_get_log(jp, count)` | Get progress log entries |

### ETA Calculation

```
avg_step_ms = elapsed_ms / completed_steps
eta_seconds = remaining_steps * avg_step_ms / 1000
```

Requires at least 2 completed steps for a meaningful estimate.
