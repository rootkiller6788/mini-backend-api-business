#ifndef WORKFLOW_ENGINE_H
#define WORKFLOW_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ddd_model.h"

#define WORKFLOW_NAME_LEN    128
#define WORKFLOW_STATE_LEN    64
#define WORKFLOW_EVENT_LEN    64
#define WORKFLOW_GUARD_LEN   256
#define WORKFLOW_MAX_STATES   32
#define WORKFLOW_MAX_TRANS    64
#define WORKFLOW_MAX_ACTIONS  16
#define WORKFLOW_MAX_TIMERS   16

typedef struct WFActionList WFActionList;

typedef int  (*wf_action_fn)(void *ctx, void *instance_data);
typedef bool (*wf_guard_fn)(void *ctx, void *instance_data);

typedef struct {
    char name[WORKFLOW_NAME_LEN];
    wf_action_fn fn;
    void *context;
} WorkflowAction;

typedef struct {
    WorkflowAction actions[WORKFLOW_MAX_ACTIONS];
    int count;
} WorkflowActionList;

typedef struct {
    char name[WORKFLOW_STATE_LEN];
    char display_name[WORKFLOW_NAME_LEN];
    bool is_initial;
    bool is_final;
    WorkflowActionList on_entry;
    WorkflowActionList on_exit;
} WorkflowState;

typedef enum {
    WF_BRANCH_SIMPLE = 0,
    WF_BRANCH_FORK,
    WF_BRANCH_JOIN,
    WF_BRANCH_TIMER
} WFBranchType;

typedef struct {
    char         source_state[WORKFLOW_STATE_LEN];
    char         target_state[WORKFLOW_STATE_LEN];
    char         event[WORKFLOW_EVENT_LEN];
    WFBranchType branch_type;
    wf_guard_fn  guard;
    void        *guard_context;
    char         guard_desc[WORKFLOW_GUARD_LEN];
    WorkflowActionList on_transition;
} WorkflowTransition;

typedef struct {
    char name[WORKFLOW_NAME_LEN];
    WorkflowState states[WORKFLOW_MAX_STATES];
    int state_count;
    WorkflowTransition transitions[WORKFLOW_MAX_TRANS];
    int transition_count;
} WorkflowDefinition;

typedef struct {
    char current_state[WORKFLOW_STATE_LEN];
    char previous_state[WORKFLOW_STATE_LEN];
    char instance_id[DDD_ID_LEN];
    void *data;
    size_t data_size;
    bool is_completed;
    bool is_forked;

    char fork_id[DDD_ID_LEN];
    int  fork_branch_count;
    char fork_parent_state[WORKFLOW_STATE_LEN];
    char fork_join_state[WORKFLOW_STATE_LEN];
    char fork_target_states[WORKFLOW_MAX_STATES][WORKFLOW_STATE_LEN];
    int  fork_target_count;

    struct WorkflowTimer *timers[WORKFLOW_MAX_TIMERS];
    int timer_count;
} WorkflowInstance;

typedef struct WorkflowTimer {
    char    timer_id[DDD_ID_LEN];
    char    event[WORKFLOW_EVENT_LEN];
    uint64_t trigger_at_ms;
    bool    triggered;
    bool    repeating;
    uint64_t interval_ms;
    struct WorkflowTimer *next;
} WorkflowTimer;

WorkflowDefinition workflow_define(const char *name);
int workflow_add_state(WorkflowDefinition *def, const char *name,
    const char *display, bool initial, bool final);
int workflow_add_state_entry_action(WorkflowDefinition *def,
    const char *state_name, wf_action_fn action, void *ctx);
int workflow_add_state_exit_action(WorkflowDefinition *def,
    const char *state_name, wf_action_fn action, void *ctx);
int workflow_add_transition(WorkflowDefinition *def,
    const char *from, const char *to, const char *event);
int workflow_add_guarded_transition(WorkflowDefinition *def,
    const char *from, const char *to, const char *event,
    wf_guard_fn guard, void *ctx, const char *desc);
int workflow_add_transition_action(WorkflowDefinition *def,
    const char *from, const char *event, wf_action_fn action, void *ctx);

int workflow_add_fork(WorkflowDefinition *def, const char *from,
    const char **target_states, int target_count, const char *join_state);
int workflow_add_join(WorkflowDefinition *def, const char *join_state,
    const char *to_state, const char *event);

int workflow_add_timer(WorkflowDefinition *def, const char *from_state,
    const char *event, uint64_t delay_ms);

WorkflowInstance workflow_instance_create(WorkflowDefinition *def,
    const char *instance_id, void *data, size_t data_size);
int workflow_instance_send_event(WorkflowInstance *inst,
    WorkflowDefinition *def, const char *event);
int workflow_instance_tick_timers(WorkflowInstance *inst,
    WorkflowDefinition *def, uint64_t now_ms);

WorkflowState        *workflow_find_state(WorkflowDefinition *def,
    const char *name);
WorkflowTransition   *workflow_find_transition(WorkflowDefinition *def,
    const char *from, const char *event);
const char           *workflow_initial_state(WorkflowDefinition *def);
bool                  workflow_is_final_state(WorkflowDefinition *def,
    const char *name);
void                  workflow_instance_destroy(WorkflowInstance *inst);

typedef enum {
    APPROVAL_DRAFT = 0,
    APPROVAL_PENDING_APPROVAL,
    APPROVAL_APPROVED,
    APPROVAL_REJECTED,
    APPROVAL_REVISION_REQUESTED
} ApprovalStateEnum;

typedef struct {
    ApprovalStateEnum state;
    char    request_id[DDD_ID_LEN];
    char    submitter[DDD_NAME_LEN];
    char    approver[DDD_NAME_LEN];
    char    title[WORKFLOW_NAME_LEN];
    char    description[DDD_DESC_LEN];
    int     revision_count;
    bool    is_urgent;
} ApprovalRequestData;

WorkflowDefinition workflow_create_approval_workflow(void);
int approval_request_submit(WorkflowInstance *inst,
    WorkflowDefinition *def);
int approval_request_approve(WorkflowInstance *inst,
    WorkflowDefinition *def);
int approval_request_reject(WorkflowInstance *inst,
    WorkflowDefinition *def);
int approval_request_request_revision(WorkflowInstance *inst,
    WorkflowDefinition *def);
int approval_request_resubmit(WorkflowInstance *inst,
    WorkflowDefinition *def);

#endif
