# mini-job-system Design Document

## Architecture Overview

```
+-------------------------------------------------------------+
|                      Application Layer                       |
|  (demos, examples, user code)                               |
+-------------------------------------------------------------+
        |              |              |              |
        v              v              v              v
+-----------+  +-----------+  +-----------+  +-----------+
|   Cron    |  |  Delayed  |  |  Worker   |  |   Task    |
| Scheduler |  |   Queue   |  |   Pool    |  |  Retry    |
+-----------+  +-----------+  +-----------+  +-----------+
        |              |              |              |
        v              v              v              v
+-------------------------------------------------------------+
|                   Job Progress Tracker                       |
|  (state machine, progress %, ETA, event log)                |
+-------------------------------------------------------------+
```

## Component Details

### 1. Cron Scheduler

**Purpose**: Execute jobs on a recurring schedule defined by cron expressions.

**Design**:
- Supports both 5-field (standard) and 6-field (with seconds) cron syntax
- Each cron field is parsed into one of 4 modes: any (\*), specific (1,3,5), range (1-5), step (\*/5)
- Field matching is O(n) where n is the number of discrete values in the field
- `next_fire_time` uses iterative search with a safety cap (~5 years of minutes)
- Catch-up mode: when a job misses scheduled firings (system was down), it can fire multiple times to catch up

**Key Data Structures**:
```
cron_field_def_t: [type, values[] up to 64, count]
cron_expression_t: [6 cron_field_def_t, catch_up flag]
cron_job_t: [id, expression, callback, last_fire, next_fire, active]
```

**Thread Safety**: Not thread-safe. Caller must serialize access or use ticks from a single thread.

### 2. Delayed Queue

**Purpose**: Schedule one-shot jobs to execute after a specified delay.

**Design**:
- Min-heap priority queue ordered by `fire_time`
- Heap operations: O(log n) for enqueue/dequeue, O(1) for peek
- Optional deduplication by unique key (key collision replaces existing entry)
- Graduated delay levels provide semantic names for common intervals
- Cancelled entries are lazily removed during dequeue

**Key Data Structures**:
```
dq_entry_t: [id, key[64], callback, userdata, fire_time, delay_ms, cancelled]
delayed_queue_t: [heap array, size, capacity, dedup_by_key flag]
```

**Deduplication**: When `dedup_by_key` is enabled, enqueuing with an existing key updates the entry in-place and re-heapifies.

**Thread Safety**: Not thread-safe. Serialize access externally.

### 3. Worker Pool

**Purpose**: Execute tasks concurrently using a fixed pool of OS threads.

**Design**:
- Fixed-size thread pool using Leader/Follower pattern
- Threads share a bounded lock-free-ish queue (mutex + condition variable)
- Workers poll with timeout to check shutdown signals
- Per-job-type concurrency limits restrict parallel execution of specific task types
- Graceful shutdown: stop accepting new tasks, wait for in-flight tasks to complete
- Heartbeat callback for external monitoring

**Key Data Structures**:
```
wp_task_t: [job_id, task_fn, userdata, ack_fn, job_type]
wp_task_queue_t: [circular buffer, mutex, condition variable]
wp_worker_t: [id, state, thread handle, current_task, last_heartbeat]
wp_concurrency_limit_t: [max_concurrent, current]
```

**Concurrency Control**:
- Tasks declare a `job_type` string at submission
- Before executing, worker checks `concurrency[job_type].current < max_concurrent`
- If at limit, worker spin-waits (10ms sleep) until slot opens
- When `max_concurrent == 0`, no limit is enforced

**Platform Support**:
- Windows: `CreateThread`, `CRITICAL_SECTION`, `CONDITION_VARIABLE`
- POSIX: `pthread_create`, `pthread_mutex_t`, `pthread_cond_t`

### 4. Task Retry

**Purpose**: Automatically retry failed tasks with exponential backoff.

**Design**:
- Each task has its own retry configuration (max retries, base delay, error filter)
- Exponential backoff: `delay = base * 2^retry_count`, capped at 1 hour
- Jitter: uniform random +/- 50ms to prevent thundering herd
- Selective retry: only retry on configured error codes
- Dead letter queue: exhausted tasks are collected and processed in batch via callback
- Retry state is tracked externally; the caller drives the retry loop

**Key Data Structures**:
```
tr_entry_t: [job_id, task_fn, max_retries, retry_count, current_delay_ms,
             next_retry_at, retry_on_codes[], dead_letter_cb, exhausted]
tr_dead_letter_t: [job_id, error_code, exhausted_at, callback]
```

**Backoff Design Rationale**:
- Base 100ms ensures fast retries for transient failures (network glitch)
- Exponential growth handles longer outages without flooding
- 1-hour cap prevents unbounded waiting
- Jitter distributes retry bursts across a 100ms window

### 5. Job Progress

**Purpose**: Track job execution progress with state machine, percentage, and ETA.

**Design**:
- 5-state finite state machine: QUEUED -> RUNNING -> SUCCEEDED/FAILED/CANCELLED
- Progress tracked as completed_steps / total_steps
- ETA estimated from average step duration
- In-memory log of up to 256 progress events
- Optional callbacks for state transitions and progress updates

**Key Data Structures**:
```
job_progress_t: [state, total_steps, completed_steps, progress_pct,
                 started_at, updated_at, completed_at,
                 eta_seconds, avg_step_ms, log[], callbacks]
jp_log_entry_t: [timestamp, completed, pct, message]
```

**State Transitions**:
- QUEUED -> RUNNING: job starts, `started_at` set
- RUNNING -> SUCCEEDED: job completes, `completed_at` set
- RUNNING -> FAILED: job fails, `completed_at` set
- RUNNING -> CANCELLED: job cancelled, `completed_at` set

No reverse or skip transitions are allowed.

## Memory Management

- All modules use `calloc` for zero-initialized allocation
- All modules provide a `destroy` function that frees owned memory
- Maximum sizes are compile-time constants (can be adjusted per-deployment)
- No dynamic resizing — fixed capacities simplify reasoning about memory

## Error Handling Strategy

- Functions return 0 on success, negative on error
- Resource exhaustion returns specific error codes (e.g., -2 for full queue)
- Invalid parameters are rejected with -1

## Limitations

| Limitation | Value |
|------------|-------|
| Max cron jobs | 64 |
| Max delayed entries | 1024 |
| Max workers | 64 |
| Max job types | 32 |
| Max retry entries | 256 |
| Max progress log entries | 256 |
| Cron search limit | ~5 years of iterations |

## Future Enhancements

- Persistent job storage (SQLite backend)
- Distributed workers (Redis/message queue)
- Job dependencies (DAG execution)
- Rate limiting per job type
- Metrics export (Prometheus)
- Web dashboard
