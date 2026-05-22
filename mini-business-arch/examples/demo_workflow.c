#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "workflow_engine.h"

static int log_on_entry(void *ctx, void *data) {
    (void)data;
    const char *state = (const char *)ctx;
    printf("  [ACTION] Entered state: %s\n", state);
    return 0;
}

static int log_on_exit(void *ctx, void *data) {
    (void)data;
    const char *state = (const char *)ctx;
    printf("  [ACTION] Exited state: %s\n", state);
    return 0;
}

static int log_transition(void *ctx, void *data) {
    (void)data;
    const char *msg = (const char *)ctx;
    printf("  [TRANSITION] %s\n", msg);
    return 0;
}

static bool check_amount_guard(void *ctx, void *data) {
    (void)ctx;
    ApprovalRequestData *req = (ApprovalRequestData *)data;
    return req->is_urgent;
}

static WorkflowDefinition build_order_workflow(void) {
    WorkflowDefinition wf = workflow_define("OrderProcessingWorkflow");

    workflow_add_state(&wf, "created", "Order Created", true, false);
    workflow_add_state(&wf, "payment_pending", "Payment Pending", false, false);
    workflow_add_state(&wf, "payment_confirmed", "Payment Confirmed", false, false);
    workflow_add_state(&wf, "picking", "Picking Items", false, false);
    workflow_add_state(&wf, "packing", "Packing Order", false, false);
    workflow_add_state(&wf, "shipped", "Shipped", false, true);
    workflow_add_state(&wf, "cancelled", "Cancelled", false, true);

    workflow_add_state_entry_action(&wf, "payment_pending",
        log_on_entry, "payment_pending");
    workflow_add_state_entry_action(&wf, "payment_confirmed",
        log_on_entry, "payment_confirmed");
    workflow_add_state_entry_action(&wf, "shipped",
        log_on_entry, "shipped");
    workflow_add_state_exit_action(&wf, "created",
        log_on_exit, "created");

    workflow_add_transition(&wf, "created", "payment_pending", "checkout");
    workflow_add_transition(&wf, "payment_pending", "payment_confirmed",
        "payment_received");
    workflow_add_transition(&wf, "payment_pending", "cancelled",
        "payment_timeout");
    workflow_add_transition(&wf, "payment_confirmed", "picking",
        "start_fulfillment");
    workflow_add_transition(&wf, "picking", "packing", "picking_complete");
    workflow_add_transition(&wf, "packing", "shipped", "ship");
    workflow_add_transition_action(&wf, "packing", "ship",
        log_transition, "Order shipped to customer");
    workflow_add_transition(&wf, "created", "cancelled", "cancel");

    return wf;
}

static void demo_basic_state_machine(void) {
    printf("--- Demo 1: Basic State Machine ---\n\n");

    WorkflowDefinition wf = build_order_workflow();

    printf("Workflow: %s\n", wf.name);
    printf("States: %d, Transitions: %d\n", wf.state_count, wf.transition_count);

    const char *initial = workflow_initial_state(&wf);
    printf("Initial state: %s\n", initial);

    for (int i = 0; i < wf.state_count; i++) {
        printf("  State[%d]: %-20s (%s) initial=%s final=%s\n",
            i, wf.states[i].name, wf.states[i].display_name,
            wf.states[i].is_initial ? "Y" : "N",
            wf.states[i].is_final ? "Y" : "N");
    }

    typedef struct {
        char order_id[64];
        double total;
        int item_count;
    } OrderData;

    OrderData od = {"ORD-001", 149.97, 3};
    WorkflowInstance inst = workflow_instance_create(
        &wf, "inst-001", &od, sizeof(od));

    printf("\nInitial current_state: %s\n", inst.current_state);

    printf("\nEvent: checkout\n");
    int rc = workflow_instance_send_event(&inst, &wf, "checkout");
    printf("  result=%d current_state=%s\n", rc, inst.current_state);

    printf("\nEvent: payment_received\n");
    rc = workflow_instance_send_event(&inst, &wf, "payment_received");
    printf("  result=%d current_state=%s\n", rc, inst.current_state);

    printf("\nEvent: start_fulfillment\n");
    rc = workflow_instance_send_event(&inst, &wf, "start_fulfillment");
    printf("  result=%d current_state=%s\n", rc, inst.current_state);

    printf("\nEvent: picking_complete\n");
    rc = workflow_instance_send_event(&inst, &wf, "picking_complete");
    printf("  result=%d current_state=%s\n", rc, inst.current_state);

    printf("\nEvent: ship\n");
    rc = workflow_instance_send_event(&inst, &wf, "ship");
    printf("  result=%d current_state=%s completed=%s\n",
        rc, inst.current_state, inst.is_completed ? "YES" : "NO");

    workflow_instance_destroy(&inst);
}

static void demo_fork_join(void) {
    printf("\n\n--- Demo 2: Fork/Join (Parallel Branches) ---\n\n");

    WorkflowDefinition wf = workflow_define("ParallelProcessing");

    workflow_add_state(&wf, "start", "Start", true, false);
    workflow_add_state(&wf, "branch_a", "Branch A", false, false);
    workflow_add_state(&wf, "branch_b", "Branch B", false, false);
    workflow_add_state(&wf, "branch_c", "Branch C", false, false);
    workflow_add_state(&wf, "joined", "Joined", false, true);

    workflow_add_state_entry_action(&wf, "branch_a", log_on_entry, "branch_a");
    workflow_add_state_entry_action(&wf, "branch_b", log_on_entry, "branch_b");
    workflow_add_state_entry_action(&wf, "branch_c", log_on_entry, "branch_c");

    const char *targets[] = {"branch_a", "branch_b", "branch_c"};
    workflow_add_fork(&wf, "start", targets, 3, "joined");
    workflow_add_join(&wf, "joined", "joined", "__JOIN_COMPLETE__");

    printf("Fork/Join workflow: %d states, %d transitions\n",
        wf.state_count, wf.transition_count);

    int dummy = 0;
    WorkflowInstance inst = workflow_instance_create(
        &wf, "fork-inst-001", &dummy, sizeof(dummy));
    printf("Initial state: %s\n", inst.current_state);

    WorkflowState *s_a = workflow_find_state(&wf, "branch_a");
    WorkflowState *s_b = workflow_find_state(&wf, "branch_b");
    printf("Found branch states: %s, %s\n",
        s_a ? s_a->name : "NULL", s_b ? s_b->name : "NULL");

    bool is_final = workflow_is_final_state(&wf, "joined");
    printf("'joined' is final: %s\n", is_final ? "yes" : "no");

    workflow_instance_destroy(&inst);
}

static void demo_approval_workflow(void) {
    printf("\n\n--- Demo 3: Approval Workflow ---\n\n");

    WorkflowDefinition wf = workflow_create_approval_workflow();
    printf("Approval workflow created: %d states, %d transitions\n",
        wf.state_count, wf.transition_count);

    ApprovalRequestData req;
    memset(&req, 0, sizeof(req));
    req.state = APPROVAL_DRAFT;
    strncpy(req.request_id, "REQ-2024-0001", DDD_ID_LEN);
    strncpy(req.submitter, "alice", DDD_NAME_LEN);
    strncpy(req.approver, "bob", DDD_NAME_LEN);
    strncpy(req.title, "New Laptop Purchase", WORKFLOW_NAME_LEN);
    strncpy(req.description, "Requesting a MacBook Pro for development work",
        DDD_DESC_LEN);
    req.is_urgent = false;

    WorkflowInstance inst = workflow_instance_create(
        &wf, "appr-inst-001", &req, sizeof(req));
    printf("Instance created: id=%s current=%s\n",
        inst.instance_id, inst.current_state);

    printf("\nSubmit...\n");
    approval_request_submit(&inst, &wf);
    ApprovalRequestData *r = (ApprovalRequestData *)inst.data;
    printf("  state=%d current_state=%s\n", r->state, inst.current_state);

    printf("\nRequest revision...\n");
    approval_request_request_revision(&inst, &wf);
    r = (ApprovalRequestData *)inst.data;
    printf("  state=%d current_state=%s revision=%d\n",
        r->state, inst.current_state, r->revision_count);

    printf("\nResubmit...\n");
    approval_request_resubmit(&inst, &wf);
    r = (ApprovalRequestData *)inst.data;
    printf("  state=%d current_state=%s\n", r->state, inst.current_state);

    printf("\nApprove...\n");
    approval_request_approve(&inst, &wf);
    r = (ApprovalRequestData *)inst.data;
    printf("  state=%d current_state=%s completed=%s\n",
        r->state, inst.current_state,
        inst.is_completed ? "YES" : "NO");

    printf("\n--- Rejected path simulation ---\n");
    ApprovalRequestData req2;
    memset(&req2, 0, sizeof(req2));
    req2.state = APPROVAL_DRAFT;
    strncpy(req2.request_id, "REQ-2024-0002", DDD_ID_LEN);
    strncpy(req2.submitter, "charlie", DDD_NAME_LEN);

    WorkflowInstance inst2 = workflow_instance_create(
        &wf, "appr-inst-002", &req2, sizeof(req2));

    approval_request_submit(&inst2, &wf);
    approval_request_reject(&inst2, &wf);
    ApprovalRequestData *r2 = (ApprovalRequestData *)inst2.data;
    printf("Rejected: state=%d current=%s completed=%s\n",
        r2->state, inst2.current_state,
        inst2.is_completed ? "YES" : "NO");

    workflow_instance_destroy(&inst);
    workflow_instance_destroy(&inst2);
}

static void demo_guarded_transitions(void) {
    printf("\n\n--- Demo 4: Guarded Transitions ---\n\n");

    WorkflowDefinition wf = workflow_create_approval_workflow();
    workflow_add_state(&wf, "expedited_review", "Expedited Review",
        false, true);

    workflow_add_guarded_transition(&wf, "pending_approval",
        "expedited_review", "expedite",
        check_amount_guard, NULL, "Urgent requests skip to expedited");

    ApprovalRequestData req_urgent;
    memset(&req_urgent, 0, sizeof(req_urgent));
    strncpy(req_urgent.request_id, "REQ-URGENT-001", DDD_ID_LEN);
    req_urgent.is_urgent = true;

    WorkflowInstance inst = workflow_instance_create(
        &wf, "urgent-inst-001", &req_urgent, sizeof(req_urgent));

    approval_request_submit(&inst, &wf);
    printf("After submit: current=%s\n", inst.current_state);

    int rc = workflow_instance_send_event(&inst, &wf, "expedite");
    printf("Expedite event: rc=%d (0=ok, -2=no-match, -3=guard-fail)\n", rc);
    printf("Current state: %s completed=%s\n",
        inst.current_state, inst.is_completed ? "YES" : "NO");

    WorkflowTransition *t = workflow_find_transition(
        &wf, "pending_approval", "expedite");
    printf("Guarded transition found: %s guard=%p\n",
        t ? "yes" : "no", t ? (void *)t->guard : NULL);

    workflow_instance_destroy(&inst);
}

static void demo_timers(void) {
    printf("\n\n--- Demo 5: Timer Events ---\n\n");

    WorkflowDefinition wf = workflow_define("TimerDemoWorkflow");

    workflow_add_state(&wf, "waiting", "Waiting", true, false);
    workflow_add_state(&wf, "timed_out", "Timed Out", false, true);
    workflow_add_state(&wf, "completed_ok", "Completed OK", false, true);

    workflow_add_transition(&wf, "waiting", "timed_out", "timeout");
    workflow_add_transition(&wf, "waiting", "completed_ok", "done");

    int dummy = 0;
    WorkflowInstance inst = workflow_instance_create(
        &wf, "timer-inst-001", &dummy, sizeof(dummy));
    printf("Initial state: %s\n", inst.current_state);

    uint64_t now = 1000000;
    workflow_instance_tick_timers(&inst, &wf, now);
    printf("Timer tick at %llu: no timers set\n", (unsigned long long)now);

    workflow_instance_send_event(&inst, &wf, "done");
    printf("After 'done' event: state=%s completed=%s\n",
        inst.current_state, inst.is_completed ? "YES" : "NO");

    workflow_instance_destroy(&inst);
}

int main(void) {
    printf("=== Workflow Engine Demos ===\n\n");

    demo_basic_state_machine();
    demo_fork_join();
    demo_approval_workflow();
    demo_guarded_transitions();
    demo_timers();

    printf("\n=== All Workflow Demos Complete ===\n");
    return 0;
}
