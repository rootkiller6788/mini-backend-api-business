#ifndef JOB_PROGRESS_H
#define JOB_PROGRESS_H

#include <stdint.h>
#include <time.h>

#define JP_MAX_LOG_ENTRIES   256
#define JP_MAX_MESSAGE_LEN   128

typedef enum {
    JP_STATE_QUEUED    = 0,
    JP_STATE_RUNNING   = 1,
    JP_STATE_SUCCEEDED = 2,
    JP_STATE_FAILED    = 3,
    JP_STATE_CANCELLED = 4
} jp_state_t;

typedef void (*jp_progress_cb)(uint64_t job_id, int completed, int total,
                               double pct, void *userdata);
typedef void (*jp_state_cb)(uint64_t job_id, jp_state_t old_state,
                            jp_state_t new_state, void *userdata);

typedef struct {
    time_t timestamp;
    int    completed;
    double pct;
    char   message[JP_MAX_MESSAGE_LEN];
} jp_log_entry_t;

typedef struct {
    uint64_t       job_id;
    jp_state_t     state;
    int            total_steps;
    int            completed_steps;
    double         progress_pct;
    time_t         started_at;
    time_t         updated_at;
    time_t         completed_at;
    double         eta_seconds;
    double         avg_step_ms;
    int            step_count_for_avg;
    jp_log_entry_t log[JP_MAX_LOG_ENTRIES];
    int            log_count;
    jp_progress_cb progress_callback;
    void          *progress_ud;
    jp_state_cb    state_callback;
    void          *state_ud;
    int            failed;
} job_progress_t;

job_progress_t *job_progress_create(void);
void            job_progress_destroy(job_progress_t *jp);

void jp_set_total_steps(job_progress_t *jp, int total);
void jp_set_progress_callback(job_progress_t *jp, jp_progress_cb cb, void *ud);
void jp_set_state_callback(job_progress_t *jp, jp_state_cb cb, void *ud);

int  jp_transition_state(job_progress_t *jp, jp_state_t new_state, const char *message);
int  jp_update_progress(job_progress_t *jp, int completed, const char *message);
int  jp_increment_progress(job_progress_t *jp, const char *message);
int  jp_fail(job_progress_t *jp, const char *message);
int  jp_cancel(job_progress_t *jp, const char *message);

jp_state_t jp_get_state(const job_progress_t *jp);
double     jp_get_progress(const job_progress_t *jp);
double     jp_get_eta(const job_progress_t *jp);
time_t     jp_get_elapsed(const job_progress_t *jp);

const jp_log_entry_t *jp_get_log(const job_progress_t *jp, int *count);

/* Utility: convert state enum to human-readable string */
const char *jp_state_name(jp_state_t state);

#endif
