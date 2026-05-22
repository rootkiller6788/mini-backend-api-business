# mini-job-system — 任务调度系统 (C 语言实现)

A lightweight, embeddable job scheduling system written in C99. Provides cron scheduling, delayed execution, worker thread pools, retry with exponential backoff, and progress tracking — all with zero external dependencies.

## Features

| Module | Capability |
|--------|-----------|
| **Cron Scheduler** | Parse 5/6-field cron expressions. Wildcard, range, step, specific values. Next-fire calculation. Catch-up for missed jobs. |
| **Delayed Queue** | Min-heap priority queue ordered by fire time. Graduated delay levels (1s, 10s, 1m, 10m, 1hr). Deduplication by unique key. |
| **Worker Pool** | Fixed-size thread pool. Leader/follower pattern. Per-job-type concurrency limits. Graceful shutdown. Heartbeat monitoring. |
| **Task Retry** | Exponential backoff with jitter. Selective retry on error codes. Dead letter queue. Configurable max retries and base delay. |
| **Job Progress** | 5-state FSM. Progress percentage. ETA estimation. In-memory progress log. State/progress callbacks. |

## Project Structure

```
mini-job-system/
├── include/
│   ├── cron_scheduler.h
│   ├── delayed_queue.h
│   ├── worker_pool.h
│   ├── task_retry.h
│   └── job_progress.h
├── src/
│   ├── cron_scheduler.c
│   ├── delayed_queue.c
│   ├── worker_pool.c
│   ├── task_retry.c
│   └── job_progress.c
├── examples/
│   ├── example_basic.c        # Basic usage of all modules
│   ├── example_cron.c         # Cron parsing and scheduling
│   └── example_retry.c        # Retry with backoff patterns
├── demos/
│   ├── demo_system.c          # Full system integration demo
│   └── demo_advanced.c        # Advanced features showcase
├── docs/
│   ├── API_REFERENCE.md       # Complete API documentation
│   └── DESIGN.md              # Architecture and design decisions
├── Makefile
└── README.md
```

## Quick Start

### Build

```sh
make
```

### Build Everything

```sh
make all
```

### Run Examples

```sh
make examples
make run-example-basic
make run-example-cron
make run-example-retry
```

### Run Demos

```sh
make demos
make run-demo-system
make run-demo-advanced
```

## Usage Snippets

### Cron Scheduling

```c
cron_scheduler_t *sched = cron_scheduler_create(NULL);

cron_register_job(sched, 1, "0 */15 * * * *", my_callback, NULL, 0);

while (running) {
    cron_tick(sched, time(NULL));
    sleep(1);
}

cron_scheduler_destroy(sched);
```

### Delayed Queue

```c
delayed_queue_t *dq = delayed_queue_create(64, 1);

dq_enqueue(dq, 100, "notify", send_notification, &data, 5000);
dq_enqueue_level(dq, 101, "cleanup", cleanup_task, NULL, DQ_LEVEL_1MIN);

dq_entry_t entry;
while (dq_dequeue(dq, time(NULL), &entry)) {
    entry.callback(entry.userdata);
}

delayed_queue_destroy(dq);
```

### Worker Pool

```c
wp_config_t cfg = { .max_workers = 4, .queue_size = 32 };
worker_pool_t *pool = worker_pool_create(&cfg);

wp_submit(pool, 1, my_task, &data, on_complete, NULL, "type_a");

wp_shutdown_graceful(pool, 5000);
worker_pool_destroy(pool);
```

### Task Retry

```c
task_retry_t *tr = task_retry_create(64);

tr_register(tr, 1, flaky_api_call, &ctx, 3, 100);
tr_error_code_t codes[] = {TR_ERR_TIMEOUT, TR_ERR_HTTP_503};
tr_set_retry_on(tr, 1, codes, 2);

int result = call_api();
int will_retry = tr_report_result(tr, 1,
    result ? TR_ERR_NONE : TR_ERR_TIMEOUT);

if (tr_can_retry(tr, 1, time(NULL))) {
    tr_trigger_retry(tr, 1, time(NULL));
}

task_retry_destroy(tr);
```

### Job Progress

```c
job_progress_t *jp = job_progress_create();
jp_set_total_steps(jp, 100);
jp_set_progress_callback(jp, on_progress, NULL);

jp_transition_state(jp, JP_STATE_RUNNING, "started");

for (int i = 0; i < 100; i++) {
    do_work();
    jp_increment_progress(jp, NULL);
}

jp_transition_state(jp, JP_STATE_SUCCEEDED, "done");
printf("ETA was: %.1fs\n", jp_get_eta(jp));

job_progress_destroy(jp);
```

## Platform Support

- **Windows**: MSVC or MinGW with Win32 threads
- **Linux/macOS**: GCC/Clang with pthreads
- **Standard**: C99

## API Documentation

See [docs/API_REFERENCE.md](docs/API_REFERENCE.md) for the complete API reference.

## Design

See [docs/DESIGN.md](docs/DESIGN.md) for architecture and design rationale.

## License

MIT
