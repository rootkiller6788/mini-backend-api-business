#include "workflow_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

WorkflowDefinition workflow_define(const char *name) {
    WorkflowDefinition def;
    memset(&def, 0, sizeof(def));
    strncpy(def.name, name, WORKFLOW_NAME_LEN);
    def.name[WORKFLOW_NAME_LEN - 1] = '\0';
    def.state_count = 0;
    def.transition_count = 0;
    return def;
}

int workflow_add_state(WorkflowDefinition *def, const char *name,
    const char *display, bool initial, bool final) {
    if (def->state_count >= WORKFLOW_MAX_STATES) return -1;
    WorkflowState *s = &def->states[def->state_count];
    memset(s, 0, sizeof(WorkflowState));
    strncpy(s->name, name, WORKFLOW_STATE_LEN);
    s->name[WORKFLOW_STATE_LEN - 1] = '\0';
    strncpy(s->display_name, display ? display : name, WORKFLOW_NAME_LEN);
    s->display_name[WORKFLOW_NAME_LEN - 1] = '\0';
    s->is_initial = initial;
    s->is_final = final;
    def->state_count++;
    return 0;
}

int workflow_add_state_entry_action(WorkflowDefinition *def,
    const char *state_name, wf_action_fn action, void *ctx) {
    WorkflowState *s = workflow_find_state(def, state_name);
    if (!s) return -1;
    if (s->on_entry.count >= WORKFLOW_MAX_ACTIONS) return -1;
    WorkflowAction *a = &s->on_entry.actions[s->on_entry.count];
    strncpy(a->name, "", WORKFLOW_NAME_LEN);
    a->fn = action;
    a->context = ctx;
    s->on_entry.count++;
    return 0;
}

int workflow_add_state_exit_action(WorkflowDefinition *def,
    const char *state_name, wf_action_fn action, void *ctx) {
    WorkflowState *s = workflow_find_state(def, state_name);
    if (!s) return -1;
    if (s->on_exit.count >= WORKFLOW_MAX_ACTIONS) return -1;
    WorkflowAction *a = &s->on_exit.actions[s->on_exit.count];
    a->fn = action;
    a->context = ctx;
    s->on_exit.count++;
    return 0;
}

int workflow_add_transition(WorkflowDefinition *def,
    const char *from, const char *to, const char *event) {
    if (def->transition_count >= WORKFLOW_MAX_TRANS) return -1;
    WorkflowTransition *t = &def->transitions[def->transition_count];
    memset(t, 0, sizeof(WorkflowTransition));
    strncpy(t->source_state, from, WORKFLOW_STATE_LEN);
    t->source_state[WORKFLOW_STATE_LEN - 1] = '\0';
    strncpy(t->target_state, to, WORKFLOW_STATE_LEN);
    t->target_state[WORKFLOW_STATE_LEN - 1] = '\0';
    strncpy(t->event, event, WORKFLOW_EVENT_LEN);
    t->event[WORKFLOW_EVENT_LEN - 1] = '\0';
    t->branch_type = WF_BRANCH_SIMPLE;
    def->transition_count++;
    return 0;
}

int workflow_add_guarded_transition(WorkflowDefinition *def,
    const char *from, const char *to, const char *event,
    wf_guard_fn guard, void *ctx, const char *desc) {
    int rc = workflow_add_transition(def, from, to, event);
    if (rc != 0) return rc;
    WorkflowTransition *t = &def->transitions[def->transition_count - 1];
    t->guard = guard;
    t->guard_context = ctx;
    if (desc) {
        strncpy(t->guard_desc, desc, WORKFLOW_GUARD_LEN);
        t->guard_desc[WORKFLOW_GUARD_LEN - 1] = '\0';
    }
    return 0;
}

int workflow_add_transition_action(WorkflowDefinition *def,
    const char *from, const char *event, wf_action_fn action, void *ctx) {
    for (int i = 0; i < def->transition_count; i++) {
        WorkflowTransition *t = &def->transitions[i];
        if (strcmp(t->source_state, from) == 0 &&
            strcmp(t->event, event) == 0) {
            if (t->on_transition.count >= WORKFLOW_MAX_ACTIONS) return -1;
            WorkflowAction *a = &t->on_transition.actions[t->on_transition.count];
            a->fn = action;
            a->context = ctx;
            t->on_transition.count++;
            return 0;
        }
    }
    return -1;
}

int workflow_add_fork(WorkflowDefinition *def, const char *from,
    const char **target_states, int target_count, const char *join_state) {
    if (def->transition_count >= WORKFLOW_MAX_TRANS) return -1;
    WorkflowTransition *t = &def->transitions[def->transition_count];
    memset(t, 0, sizeof(WorkflowTransition));
    strncpy(t->source_state, from, WORKFLOW_STATE_LEN);
    t->branch_type = WF_BRANCH_FORK;
    strncpy(t->event, "__FORK__", WORKFLOW_EVENT_LEN);
    strncpy(t->target_state, join_state, WORKFLOW_STATE_LEN);
    t->target_state[WORKFLOW_STATE_LEN - 1] = '\0';
    def->transition_count++;
    for (int i = 0; i < target_count; i++) {
        if (def->transition_count >= WORKFLOW_MAX_TRANS) break;
        WorkflowTransition *ti = &def->transitions[def->transition_count];
        memset(ti, 0, sizeof(WorkflowTransition));
        strncpy(ti->source_state, from, WORKFLOW_STATE_LEN);
        strncpy(ti->target_state, target_states[i], WORKFLOW_STATE_LEN);
        ti->target_state[WORKFLOW_STATE_LEN - 1] = '\0';
        strncpy(ti->event, "__FORK_BRANCH__", WORKFLOW_EVENT_LEN);
        ti->branch_type = WF_BRANCH_SIMPLE;
        def->transition_count++;
    }
    return 0;
}

int workflow_add_join(WorkflowDefinition *def, const char *join_state,
    const char *to_state, const char *event) {
    if (def->transition_count >= WORKFLOW_MAX_TRANS) return -1;
    WorkflowTransition *t = &def->transitions[def->transition_count];
    memset(t, 0, sizeof(WorkflowTransition));
    strncpy(t->source_state, join_state, WORKFLOW_STATE_LEN);
    strncpy(t->target_state, to_state, WORKFLOW_STATE_LEN);
    strncpy(t->event, event, WORKFLOW_EVENT_LEN);
    t->branch_type = WF_BRANCH_JOIN;
    def->transition_count++;
    return 0;
}

int workflow_add_timer(WorkflowDefinition *def, const char *from_state,
    const char *event, uint64_t delay_ms) {
    (void)delay_ms;
    return workflow_add_transition(def, from_state, from_state, event);
}

WorkflowInstance workflow_instance_create(WorkflowDefinition *def,
    const char *instance_id, void *data, size_t data_size) {
    WorkflowInstance inst;
    memset(&inst, 0, sizeof(inst));
    const char *initial = workflow_initial_state(def);
    strncpy(inst.current_state, initial, WORKFLOW_STATE_LEN);
    inst.current_state[WORKFLOW_STATE_LEN - 1] = '\0';
    inst.previous_state[0] = '\0';
    strncpy(inst.instance_id, instance_id, DDD_ID_LEN);
    inst.instance_id[DDD_ID_LEN - 1] = '\0';
    inst.data = malloc(data_size);
    memcpy(inst.data, data, data_size);
    inst.data_size = data_size;
    inst.is_completed = false;
    inst.is_forked = false;
    inst.timer_count = 0;
    return inst;
}

static void transition_execute_actions(WorkflowTransition *t,
    void *instance_data) {
    for (int i = 0; i < t->on_transition.count; i++) {
        t->on_transition.actions[i].fn(
            t->on_transition.actions[i].context, instance_data);
    }
}

static void state_execute_on_exit(WorkflowState *s, void *instance_data) {
    for (int i = 0; i < s->on_exit.count; i++) {
        s->on_exit.actions[i].fn(s->on_exit.actions[i].context, instance_data);
    }
}

static void state_execute_on_entry(WorkflowState *s, void *instance_data) {
    for (int i = 0; i < s->on_entry.count; i++) {
        s->on_entry.actions[i].fn(
            s->on_entry.actions[i].context, instance_data);
    }
}

int workflow_instance_send_event(WorkflowInstance *inst,
    WorkflowDefinition *def, const char *event) {
    if (inst->is_completed) return -1;

    WorkflowTransition *t = workflow_find_transition(
        def, inst->current_state, event);
    if (!t) return -2;

    bool guard_passed = true;
    if (t->guard) {
        guard_passed = t->guard(t->guard_context, inst->data);
    }
    if (!guard_passed) return -3;

    strncpy(inst->previous_state, inst->current_state, WORKFLOW_STATE_LEN);
    inst->previous_state[WORKFLOW_STATE_LEN - 1] = '\0';

    WorkflowState *src = workflow_find_state(def, inst->current_state);
    if (src) state_execute_on_exit(src, inst->data);

    transition_execute_actions(t, inst->data);

    strncpy(inst->current_state, t->target_state, WORKFLOW_STATE_LEN);
    inst->current_state[WORKFLOW_STATE_LEN - 1] = '\0';

    WorkflowState *dst = workflow_find_state(def, inst->current_state);
    if (dst) {
        state_execute_on_entry(dst, inst->data);
        if (dst->is_final) {
            inst->is_completed = true;
        }
    }

    return 0;
}

int workflow_instance_tick_timers(WorkflowInstance *inst,
    WorkflowDefinition *def, uint64_t now_ms) {
    for (int i = 0; i < inst->timer_count; i++) {
        WorkflowTimer *timer = inst->timers[i];
        if (!timer->triggered && now_ms >= timer->trigger_at_ms) {
            timer->triggered = true;
            workflow_instance_send_event(inst, def, timer->event);
            if (timer->repeating) {
                timer->trigger_at_ms = now_ms + timer->interval_ms;
                timer->triggered = false;
            }
        }
    }
    return 0;
}

WorkflowState *workflow_find_state(WorkflowDefinition *def, const char *name) {
    for (int i = 0; i < def->state_count; i++) {
        if (strcmp(def->states[i].name, name) == 0) {
            return &def->states[i];
        }
    }
    return NULL;
}

WorkflowTransition *workflow_find_transition(WorkflowDefinition *def,
    const char *from, const char *event) {
    for (int i = 0; i < def->transition_count; i++) {
        if (strcmp(def->transitions[i].source_state, from) == 0 &&
            strcmp(def->transitions[i].event, event) == 0) {
            return &def->transitions[i];
        }
    }
    return NULL;
}

const char *workflow_initial_state(WorkflowDefinition *def) {
    for (int i = 0; i < def->state_count; i++) {
        if (def->states[i].is_initial) return def->states[i].name;
    }
    return NULL;
}

bool workflow_is_final_state(WorkflowDefinition *def, const char *name) {
    WorkflowState *s = workflow_find_state(def, name);
    return s && s->is_final;
}

void workflow_instance_destroy(WorkflowInstance *inst) {
    free(inst->data);
    inst->data = NULL;
}

static int appr_log_action(void *ctx, void *instance_data) {
    (void)ctx;
    ApprovalRequestData *req = (ApprovalRequestData *)instance_data;
    printf("[APPROVAL] %s state=%d revision=%d\n",
        req->request_id, req->state, req->revision_count);
    return 0;
}

WorkflowDefinition workflow_create_approval_workflow(void) {
    WorkflowDefinition wf = workflow_define("ApprovalWorkflow");

    workflow_add_state(&wf, "draft", "Draft", true, false);
    workflow_add_state(&wf, "pending_approval", "Pending Approval", false, false);
    workflow_add_state(&wf, "approved", "Approved", false, true);
    workflow_add_state(&wf, "rejected", "Rejected", false, true);
    workflow_add_state(&wf, "revision_requested", "Revision Requested", false, false);

    workflow_add_state_entry_action(&wf, "pending_approval",
        appr_log_action, NULL);
    workflow_add_state_entry_action(&wf, "approved", appr_log_action, NULL);
    workflow_add_state_entry_action(&wf, "rejected", appr_log_action, NULL);

    workflow_add_transition(&wf, "draft", "pending_approval", "submit");
    workflow_add_transition(&wf, "pending_approval", "approved", "approve");
    workflow_add_transition(&wf, "pending_approval", "rejected", "reject");
    workflow_add_transition(&wf, "pending_approval", "revision_requested",
        "request_revision");
    workflow_add_transition(&wf, "revision_requested", "pending_approval",
        "resubmit");

    return wf;
}

int approval_request_submit(WorkflowInstance *inst, WorkflowDefinition *def) {
    ApprovalRequestData *req = (ApprovalRequestData *)inst->data;
    req->state = APPROVAL_PENDING_APPROVAL;
    return workflow_instance_send_event(inst, def, "submit");
}

int approval_request_approve(WorkflowInstance *inst, WorkflowDefinition *def) {
    ApprovalRequestData *req = (ApprovalRequestData *)inst->data;
    req->state = APPROVAL_APPROVED;
    return workflow_instance_send_event(inst, def, "approve");
}

int approval_request_reject(WorkflowInstance *inst, WorkflowDefinition *def) {
    ApprovalRequestData *req = (ApprovalRequestData *)inst->data;
    req->state = APPROVAL_REJECTED;
    return workflow_instance_send_event(inst, def, "reject");
}

int approval_request_request_revision(WorkflowInstance *inst,
    WorkflowDefinition *def) {
    ApprovalRequestData *req = (ApprovalRequestData *)inst->data;
    req->state = APPROVAL_REVISION_REQUESTED;
    req->revision_count++;
    return workflow_instance_send_event(inst, def, "request_revision");
}

int approval_request_resubmit(WorkflowInstance *inst,
    WorkflowDefinition *def) {
    ApprovalRequestData *req = (ApprovalRequestData *)inst->data;
    req->state = APPROVAL_PENDING_APPROVAL;
    return workflow_instance_send_event(inst, def, "resubmit");
}
