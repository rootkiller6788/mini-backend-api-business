#include "delayed_queue.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

delayed_queue_t *delayed_queue_create(int capacity, int dedup)
{
    delayed_queue_t *dq;
    if (capacity <= 0) capacity = DQ_MAX_ENTRIES;

    dq = (delayed_queue_t *)calloc(1, sizeof(*dq));
    if (!dq) return NULL;

    dq->heap = (dq_entry_t *)calloc(capacity, sizeof(dq_entry_t));
    if (!dq->heap) { free(dq); return NULL; }

    dq->capacity     = capacity;
    dq->size         = 0;
    dq->dedup_by_key = dedup;
    return dq;
}

void delayed_queue_destroy(delayed_queue_t *dq)
{
    if (dq) {
        free(dq->heap);
        free(dq);
    }
}

void dq_heap_sift_up(dq_entry_t *heap, int idx)
{
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (heap[idx].fire_time >= heap[parent].fire_time)
            break;
        dq_entry_t tmp = heap[idx];
        heap[idx]      = heap[parent];
        heap[parent]   = tmp;
        idx            = parent;
    }
}

void dq_heap_sift_down(dq_entry_t *heap, int idx, int size)
{
    while (1) {
        int left  = 2 * idx + 1;
        int right = 2 * idx + 2;
        int smallest = idx;

        if (left < size && heap[left].fire_time < heap[smallest].fire_time)
            smallest = left;
        if (right < size && heap[right].fire_time < heap[smallest].fire_time)
            smallest = right;

        if (smallest == idx) break;

        dq_entry_t tmp = heap[idx];
        heap[idx]      = heap[smallest];
        heap[smallest] = tmp;
        idx            = smallest;
    }
}

static int dq_find_by_key(delayed_queue_t *dq, const char *key)
{
    int i;
    if (!key || !key[0]) return -1;
    for (i = 0; i < dq->size; i++) {
        if (strcmp(dq->heap[i].key, key) == 0)
            return i;
    }
    return -1;
}

static int dq_find_by_id(delayed_queue_t *dq, uint64_t id)
{
    int i;
    for (i = 0; i < dq->size; i++) {
        if (dq->heap[i].id == id)
            return i;
    }
    return -1;
}

int dq_enqueue(delayed_queue_t *dq, uint64_t id, const char *key,
               dq_job_fn cb, void *ud, uint64_t delay_ms)
{
    if (!dq || !cb) return -1;
    if (dq->size >= dq->capacity) return -2;

    if (dq->dedup_by_key && key && key[0]) {
        int idx = dq_find_by_key(dq, key);
        if (idx >= 0) {
            dq->heap[idx].id          = id;
            dq->heap[idx].callback    = cb;
            dq->heap[idx].userdata    = ud;
            dq->heap[idx].fire_time   = (time_t)(time(NULL) + delay_ms / 1000);
            dq->heap[idx].delay_ms    = delay_ms;
            dq->heap[idx].cancelled   = 0;
            dq_heap_sift_up(dq->heap, idx);
            dq_heap_sift_down(dq->heap, idx, dq->size);
            return 0;
        }
    }

    dq_entry_t *entry = &dq->heap[dq->size];
    entry->id        = id;
    entry->callback  = cb;
    entry->userdata  = ud;
    entry->fire_time = (time_t)(time(NULL) + delay_ms / 1000);
    if (entry->fire_time <= time(NULL))
        entry->fire_time = time(NULL) + 1;
    entry->delay_ms  = delay_ms;
    entry->cancelled = 0;

    if (key)
        strncpy(entry->key, key, DQ_KEY_LEN - 1);
    else
        entry->key[0] = '\0';
    entry->key[DQ_KEY_LEN - 1] = '\0';

    dq->size++;
    dq_heap_sift_up(dq->heap, dq->size - 1);
    return 0;
}

int dq_enqueue_level(delayed_queue_t *dq, uint64_t id, const char *key,
                     dq_job_fn cb, void *ud, dq_delay_level_t level)
{
    return dq_enqueue(dq, id, key, cb, ud, (uint64_t)level);
}

int dq_cancel(delayed_queue_t *dq, uint64_t id)
{
    int idx = dq_find_by_id(dq, id);
    if (idx < 0) return -1;
    dq->heap[idx].cancelled = 1;
    return 0;
}

int dq_cancel_by_key(delayed_queue_t *dq, const char *key)
{
    int idx = dq_find_by_key(dq, key);
    if (idx < 0) return -1;
    dq->heap[idx].cancelled = 1;
    return 0;
}

int dq_dequeue(delayed_queue_t *dq, time_t now, dq_entry_t *out)
{
    if (!dq || dq->size == 0 || !out) return 0;

    if (dq->heap[0].cancelled) {
        dq->heap[0] = dq->heap[dq->size - 1];
        dq->size--;
        if (dq->size > 0)
            dq_heap_sift_down(dq->heap, 0, dq->size);
        return dq_dequeue(dq, now, out);
    }

    if (dq->heap[0].fire_time > now)
        return 0;

    *out = dq->heap[0];
    dq->heap[0] = dq->heap[dq->size - 1];
    dq->size--;
    if (dq->size > 0)
        dq_heap_sift_down(dq->heap, 0, dq->size);
    return 1;
}

int dq_peek(delayed_queue_t *dq, dq_entry_t *out)
{
    if (!dq || dq->size == 0 || !out) return 0;
    *out = dq->heap[0];
    return 1;
}

int dq_is_empty(const delayed_queue_t *dq)
{
    return !dq || dq->size == 0;
}

int dq_size(const delayed_queue_t *dq)
{
    return dq ? dq->size : 0;
}

uint64_t dq_next_delay_ms(const delayed_queue_t *dq, time_t now)
{
    if (!dq || dq->size == 0) return 0;
    if (dq->heap[0].fire_time <= now) return 0;
    return (uint64_t)(dq->heap[0].fire_time - now) * 1000;
}
