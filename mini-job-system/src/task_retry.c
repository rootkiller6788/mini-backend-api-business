#include "task_retry.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

task_retry_t *task_retry_create(int capacity)
{
    task_retry_t *tr;
    if (capacity <= 0) capacity = TR_DEAD_LETTER_MAX;

    tr = (task_retry_t *)calloc(1, sizeof(*tr));
    if (!tr) return NULL;

    tr->capacity = capacity;
    tr->count    = 0;
    tr->dead_count = 0;
    tr->seed      = (unsigned int)time(NULL);
    return tr;
}

void task_retry_destroy(task_retry_t *tr)
{
    if (tr) free(tr);
}

static int tr_find(task_retry_t *tr, uint64_t job_id)
{
    int i;
    for (i = 0; i < tr->count; i++)
        if (tr->entries[i].job_id == job_id)
            return i;
    return -1;
}

int tr_register(task_retry_t *tr, uint64_t job_id, tr_task_fn task, void *ud,
                int max_retries, uint64_t base_delay_ms)
{
    int idx;
    if (!tr || !task) return -1;
    if (tr->count >= tr->capacity) return -2;

    idx = tr_find(tr, job_id);
    if (idx < 0) {
        idx = tr->count++;
        memset(&tr->entries[idx], 0, sizeof(tr_entry_t));
    }

    tr->entries[idx].job_id         = job_id;
    tr->entries[idx].task           = task;
    tr->entries[idx].userdata       = ud;
    tr->entries[idx].max_retries    = (max_retries >= 0) ? max_retries
                                     : TR_MAX_RETRIES_DEFAULT;
    tr->entries[idx].base_delay_ms  = base_delay_ms ? base_delay_ms
                                     : TR_BASE_DELAY_MS;
    tr->entries[idx].current_delay_ms = tr->entries[idx].base_delay_ms;
    tr->entries[idx].retry_count    = 0;
    tr->entries[idx].exhausted      = 0;
    tr->entries[idx].retry_on_count = 0;
    return 0;
}

int tr_set_retry_on(task_retry_t *tr, uint64_t job_id,
                    const tr_error_code_t *codes, int count)
{
    int idx = tr_find(tr, job_id);
    int i;
    if (idx < 0) return -1;
    tr->entries[idx].retry_on_count = count > 8 ? 8 : count;
    for (i = 0; i < tr->entries[idx].retry_on_count; i++)
        tr->entries[idx].retry_on_codes[i] = codes[i];
    return 0;
}

int tr_set_dead_letter(task_retry_t *tr, uint64_t job_id,
                       tr_dead_letter_fn cb, void *ud)
{
    int idx = tr_find(tr, job_id);
    if (idx < 0) return -1;
    tr->entries[idx].dead_letter_cb = cb;
    tr->entries[idx].dead_letter_ud = ud;
    return 0;
}

static int should_retry_on(const tr_entry_t *entry, tr_error_code_t err)
{
    int i;
    if (err == TR_ERR_NONE) return 0;
    if (entry->retry_on_count == 0) return 1;
    for (i = 0; i < entry->retry_on_count; i++)
        if (entry->retry_on_codes[i] == (int)err)
            return 1;
    return 0;
}

uint64_t tr_calc_delay(int retry_count, uint64_t base_ms, int use_jitter,
                       unsigned int *seed)
{
    uint64_t delay = base_ms;
    int i;
    for (i = 0; i < retry_count; i++) {
        delay *= 2;
        if (delay > TR_MAX_DELAY_MS) {
            delay = TR_MAX_DELAY_MS;
            break;
        }
    }

    if (use_jitter && seed) {
        /* Simple LCG (Linear Congruential Generator) for portability.
         * Parameters from Numerical Recipes: a=1664525, c=1013904223, m=2^32 */
        *seed = (*seed) * 1664525u + 1013904223u;
        int jitter = (int)(((double)(*seed) / 4294967296.0)
                           * (double)TR_MAX_JITTER_MS * 2.0
                           - (double)TR_MAX_JITTER_MS);
        delay = (uint64_t)((int64_t)delay + (int64_t)jitter);
        if (delay < 1) delay = 1;
        if (delay > TR_MAX_DELAY_MS) delay = TR_MAX_DELAY_MS;
    }
    return delay;
}

int tr_report_result(task_retry_t *tr, uint64_t job_id, tr_error_code_t err)
{
    int idx = tr_find(tr, job_id);
    if (idx < 0) return -1;

    tr_entry_t *entry = &tr->entries[idx];

    if (err == TR_ERR_NONE || !should_retry_on(entry, err)) {
        entry->exhausted = 1;
        return 0;
    }

    if (entry->retry_count >= entry->max_retries) {
        entry->exhausted = 1;
        if (tr->dead_count < TR_DEAD_LETTER_MAX) {
            tr_dead_letter_t *dl = &tr->dead_letters[tr->dead_count++];
            dl->job_id        = job_id;
            dl->error         = err;
            dl->exhausted_at  = time(NULL);
            dl->dead_letter_cb = entry->dead_letter_cb;
            dl->dead_letter_ud = entry->dead_letter_ud;
        }
        return 0;
    }

    entry->retry_count++;  /* Count this retry attempt */
    entry->current_delay_ms = tr_calc_delay(entry->retry_count,
                                             entry->base_delay_ms, 1,
                                             &tr->seed);
    entry->next_retry_at = time(NULL)
                          + (time_t)(entry->current_delay_ms / 1000);
    if (entry->next_retry_at <= time(NULL))
        entry->next_retry_at = time(NULL) + 1;
    return 1;
}

int tr_can_retry(const task_retry_t *tr, uint64_t job_id, time_t now)
{
    int idx = tr_find((task_retry_t *)tr, job_id);
    if (idx < 0) return 0;
    if (tr->entries[idx].exhausted) return 0;
    return tr->entries[idx].next_retry_at <= now;
}

int tr_trigger_retry(task_retry_t *tr, uint64_t job_id, time_t now)
{
    int idx = tr_find(tr, job_id);
    if (idx < 0) return 0;
    if (tr->entries[idx].exhausted) return 0;
    if (tr->entries[idx].next_retry_at > now) return 0;

    tr->entries[idx].task(tr->entries[idx].userdata);
    return 1;
}

int tr_get_retry_count(const task_retry_t *tr, uint64_t job_id)
{
    int idx = tr_find((task_retry_t *)tr, job_id);
    return idx >= 0 ? tr->entries[idx].retry_count : -1;
}

int tr_is_exhausted(const task_retry_t *tr, uint64_t job_id)
{
    int idx = tr_find((task_retry_t *)tr, job_id);
    return idx >= 0 ? tr->entries[idx].exhausted : 0;
}

void tr_process_dead_letters(task_retry_t *tr)
{
    int i;
    if (!tr) return;
    for (i = 0; i < tr->dead_count; i++) {
        tr_dead_letter_t *dl = &tr->dead_letters[i];
        if (dl->dead_letter_cb)
            dl->dead_letter_cb(dl->job_id, dl->error, dl->dead_letter_ud);
    }
    tr->dead_count = 0;
}
