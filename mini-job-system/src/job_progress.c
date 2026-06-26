#include "job_progress.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

job_progress_t *job_progress_create(void)
{
    job_progress_t *jp = (job_progress_t *)calloc(1, sizeof(*jp));
    if (!jp) return NULL;
    jp->state         = JP_STATE_QUEUED;
    jp->total_steps   = 0;
    jp->completed_steps = 0;
    jp->progress_pct  = 0.0;
    jp->log_count     = 0;
    jp->failed        = 0;
    return jp;
}

void job_progress_destroy(job_progress_t *jp)
{
    if (jp) free(jp);
}

void jp_set_total_steps(job_progress_t *jp, int total)
{
    if (!jp) return;
    jp->total_steps = total > 0 ? total : 0;
}

void jp_set_progress_callback(job_progress_t *jp, jp_progress_cb cb, void *ud)
{
    if (!jp) return;
    jp->progress_callback = cb;
    jp->progress_ud       = ud;
}

void jp_set_state_callback(job_progress_t *jp, jp_state_cb cb, void *ud)
{
    if (!jp) return;
    jp->state_callback = cb;
    jp->state_ud       = ud;
}

static const char *state_name(jp_state_t state)
{
    switch (state) {
        case JP_STATE_QUEUED:    return "QUEUED";
        case JP_STATE_RUNNING:   return "RUNNING";
        case JP_STATE_SUCCEEDED: return "SUCCEEDED";
        case JP_STATE_FAILED:    return "FAILED";
        case JP_STATE_CANCELLED: return "CANCELLED";
        default:                 return "UNKNOWN";
    }
}

/* Convert job_progress state to string for public introspection */
const char *jp_state_name(jp_state_t state)
{
    return state_name(state);
}

int jp_transition_state(job_progress_t *jp, jp_state_t new_state,
                        const char *message)
{
    if (!jp) return -1;
    jp_state_t old = jp->state;
    jp->state = new_state;

    time_t now = time(NULL);

    switch (new_state) {
        case JP_STATE_RUNNING:
            jp->started_at = now;
            break;
        case JP_STATE_SUCCEEDED:
        case JP_STATE_FAILED:
        case JP_STATE_CANCELLED:
            jp->completed_at = now;
            if (jp->completed_steps > 0 && jp->started_at > 0) {
                double elapsed = difftime(now, jp->started_at);
                if (elapsed > 0)
                    jp->avg_step_ms = (elapsed * 1000.0)
                                     / (double)jp->completed_steps;
            }
            break;
        default:
            break;
    }

    if (jp->state_callback)
        jp->state_callback(jp->job_id, old, new_state, jp->state_ud);

    if (message && jp->log_count < JP_MAX_LOG_ENTRIES) {
        jp_log_entry_t *entry = &jp->log[jp->log_count++];
        entry->timestamp = now;
        entry->completed = jp->completed_steps;
        entry->pct       = jp->progress_pct;
        strncpy(entry->message, message, JP_MAX_MESSAGE_LEN - 1);
        entry->message[JP_MAX_MESSAGE_LEN - 1] = '\0';
    }
    return 0;
}

int jp_update_progress(job_progress_t *jp, int completed, const char *message)
{
    if (!jp) return -1;
    if (jp->state != JP_STATE_RUNNING) return -2;

    jp->completed_steps = completed < 0 ? 0 : completed;

    if (jp->total_steps > 0) {
        jp->progress_pct = ((double)jp->completed_steps
                           / (double)jp->total_steps) * 100.0;
        if (jp->progress_pct > 100.0) jp->progress_pct = 100.0;
    } else {
        jp->progress_pct = 0.0;
    }

    time_t now = time(NULL);
    jp->updated_at = now;

    if (jp->completed_steps > 1 && jp->started_at > 0) {
        double elapsed = difftime(now, jp->started_at);
        if (elapsed > 0) {
            jp->avg_step_ms = (elapsed * 1000.0)
                             / (double)jp->completed_steps;
            if (jp->total_steps > 0) {
                int remaining = jp->total_steps - jp->completed_steps;
                jp->eta_seconds = (double)remaining * jp->avg_step_ms
                                 / 1000.0;
            }
        }
    }

    if (jp->progress_callback)
        jp->progress_callback(jp->job_id, jp->completed_steps,
                              jp->total_steps, jp->progress_pct,
                              jp->progress_ud);

    if (message && jp->log_count < JP_MAX_LOG_ENTRIES) {
        jp_log_entry_t *entry = &jp->log[jp->log_count++];
        entry->timestamp = now;
        entry->completed = jp->completed_steps;
        entry->pct       = jp->progress_pct;
        strncpy(entry->message, message, JP_MAX_MESSAGE_LEN - 1);
        entry->message[JP_MAX_MESSAGE_LEN - 1] = '\0';
    }
    return 0;
}

int jp_increment_progress(job_progress_t *jp, const char *message)
{
    if (!jp) return -1;
    return jp_update_progress(jp, jp->completed_steps + 1, message);
}

int jp_fail(job_progress_t *jp, const char *message)
{
    if (!jp) return -1;
    jp->failed = 1;
    return jp_transition_state(jp, JP_STATE_FAILED, message);
}

int jp_cancel(job_progress_t *jp, const char *message)
{
    if (!jp) return -1;
    return jp_transition_state(jp, JP_STATE_CANCELLED, message);
}

jp_state_t jp_get_state(const job_progress_t *jp)
{
    return jp ? jp->state : JP_STATE_FAILED;
}

double jp_get_progress(const job_progress_t *jp)
{
    return jp ? jp->progress_pct : 0.0;
}

double jp_get_eta(const job_progress_t *jp)
{
    return jp ? jp->eta_seconds : 0.0;
}

time_t jp_get_elapsed(const job_progress_t *jp)
{
    if (!jp || jp->started_at == 0) return 0;
    if (jp->completed_at > 0)
        return (time_t)difftime(jp->completed_at, jp->started_at);
    return (time_t)difftime(time(NULL), jp->started_at);
}

const jp_log_entry_t *jp_get_log(const job_progress_t *jp, int *count)
{
    if (!jp) { if (count) *count = 0; return NULL; }
    if (count) *count = jp->log_count;
    return jp->log;
}
