#ifndef TASK_RETRY_H
#define TASK_RETRY_H

#include <stdint.h>
#include <time.h>

#define TR_MAX_RETRIES_DEFAULT   3
#define TR_BASE_DELAY_MS         100
#define TR_MAX_DELAY_MS          3600000
#define TR_MAX_JITTER_MS         50
#define TR_DEAD_LETTER_MAX       256

typedef enum {
    TR_ERR_NONE             = 0,
    TR_ERR_TIMEOUT          = 1,
    TR_ERR_HTTP_503         = 2,
    TR_ERR_CONN_REFUSED     = 3,
    TR_ERR_TEMP_FAILURE     = 4,
    TR_ERR_UNKNOWN          = 99
} tr_error_code_t;

typedef void (*tr_task_fn)(void *userdata);
typedef void (*tr_dead_letter_fn)(uint64_t job_id, tr_error_code_t err, void *userdata);

typedef struct {
    uint64_t          job_id;
    tr_task_fn        task;
    void             *userdata;
    int               max_retries;
    int               retry_count;
    uint64_t          base_delay_ms;
    uint64_t          current_delay_ms;
    int               retry_on_codes[8];
    int               retry_on_count;
    time_t            next_retry_at;
    tr_dead_letter_fn dead_letter_cb;
    void             *dead_letter_ud;
    int               exhausted;
} tr_entry_t;

typedef struct {
    uint64_t          job_id;
    tr_error_code_t   error;
    time_t            exhausted_at;
    tr_dead_letter_fn dead_letter_cb;
    void             *dead_letter_ud;
} tr_dead_letter_t;

typedef struct {
    tr_entry_t      entries[TR_DEAD_LETTER_MAX];
    int             count;
    int             capacity;
    tr_dead_letter_t dead_letters[TR_DEAD_LETTER_MAX];
    int             dead_count;
    unsigned int    seed;
} task_retry_t;

task_retry_t *task_retry_create(int capacity);
void          task_retry_destroy(task_retry_t *tr);

int  tr_register(task_retry_t *tr, uint64_t job_id, tr_task_fn task, void *ud,
                 int max_retries, uint64_t base_delay_ms);
int  tr_set_retry_on(task_retry_t *tr, uint64_t job_id,
                     const tr_error_code_t *codes, int count);
int  tr_set_dead_letter(task_retry_t *tr, uint64_t job_id,
                        tr_dead_letter_fn cb, void *ud);

int  tr_report_result(task_retry_t *tr, uint64_t job_id, tr_error_code_t err);
int  tr_can_retry(const task_retry_t *tr, uint64_t job_id, time_t now);
int  tr_trigger_retry(task_retry_t *tr, uint64_t job_id, time_t now);

int  tr_get_retry_count(const task_retry_t *tr, uint64_t job_id);
int  tr_is_exhausted(const task_retry_t *tr, uint64_t job_id);

uint64_t tr_calc_delay(int retry_count, uint64_t base_ms, int use_jitter, unsigned int *seed);

void tr_process_dead_letters(task_retry_t *tr);

#endif
