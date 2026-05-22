#ifndef DELAYED_QUEUE_H
#define DELAYED_QUEUE_H

#include <stdint.h>
#include <time.h>

#define DQ_MAX_ENTRIES      1024
#define DQ_KEY_LEN          64

typedef enum {
    DQ_LEVEL_1S    = 1000,
    DQ_LEVEL_10S   = 10000,
    DQ_LEVEL_1MIN  = 60000,
    DQ_LEVEL_10MIN = 600000,
    DQ_LEVEL_1HR   = 3600000
} dq_delay_level_t;

typedef void (*dq_job_fn)(void *userdata);

typedef struct {
    uint64_t  id;
    char      key[DQ_KEY_LEN];
    dq_job_fn callback;
    void     *userdata;
    time_t    fire_time;
    uint64_t  delay_ms;
    int       cancelled;
} dq_entry_t;

typedef struct {
    dq_entry_t *heap;
    int         size;
    int         capacity;
    int         dedup_by_key;
} delayed_queue_t;

delayed_queue_t *delayed_queue_create(int capacity, int dedup);
void             delayed_queue_destroy(delayed_queue_t *dq);

int  dq_enqueue(delayed_queue_t *dq, uint64_t id, const char *key,
                dq_job_fn cb, void *ud, uint64_t delay_ms);
int  dq_enqueue_level(delayed_queue_t *dq, uint64_t id, const char *key,
                      dq_job_fn cb, void *ud, dq_delay_level_t level);
int  dq_cancel(delayed_queue_t *dq, uint64_t id);
int  dq_cancel_by_key(delayed_queue_t *dq, const char *key);

int  dq_dequeue(delayed_queue_t *dq, time_t now, dq_entry_t *out);
int  dq_peek(delayed_queue_t *dq, dq_entry_t *out);
int  dq_is_empty(const delayed_queue_t *dq);
int  dq_size(const delayed_queue_t *dq);

uint64_t dq_next_delay_ms(const delayed_queue_t *dq, time_t now);

void dq_heap_sift_up(dq_entry_t *heap, int idx);
void dq_heap_sift_down(dq_entry_t *heap, int idx, int size);

#endif
