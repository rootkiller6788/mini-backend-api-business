#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "workflow_engine.h"

static int failures = 0;
#define TEST(name) printf("  %-55s", name)
#define CHECK(cond) do { \
    if (!(cond)) { printf(" FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else printf(" PASS\n"); \
} while(0)

static void test_workflow_definition(void) {
    TEST("workflow_define creates empty workflow");
    WorkflowDefinition wf = workflow_define("TestWF");
    CHECK(strcmp(wf.name, "TestWF") == 0);
    CHECK(wf.state_count == 0);
    CHECK(wf.transition_count == 0);

    TEST("workflow_add_state adds a state");
    int rc = workflow_add_state(&wf, "init", "Initial", true, false);
    CHECK(rc == 0);
    CHECK(wf.state_count == 1);

    TEST("workflow_add_state adds another state");
    rc = workflow_add_state(&wf, "done", "Done", false, true);
    CHECK(rc == 0);
    CHECK(wf.state_count == 2);

    TEST("workflow_find_state finds state by name");
    WorkflowState *s = workflow_find_state(&wf, "init");
    CHECK(s != NULL);
    CHECK(s->is_initial);

    TEST("workflow_find_state returns NULL for unknown");
    s = workflow_find_state(&wf, "nope");
    CHECK(s == NULL);

    TEST("workflow_initial_state returns initial state");
    const char *init = workflow_initial_state(&wf);
    CHECK(init != NULL);
    CHECK(strcmp(init, "init") == 0);

    TEST("workflow_is_final_state checks correctly");
    CHECK(workflow_is_final_state(&wf, "done"));
    CHECK(!workflow_is_final_state(&wf, "init"));
}

static void test_workflow_transitions(void) {
    TEST("workflow_add_transition creates transition");
    WorkflowDefinition wf = workflow_define("TransTest");
    workflow_add_state(&wf, "s1", "State1", true, false);
    workflow_add_state(&wf, "s2", "State2", false, false);
    workflow_add_state(&wf, "s3", "State3", false, true);
    int rc = workflow_add_transition(&wf, "s1", "s2", "go");
    CHECK(rc == 0);
    CHECK(wf.transition_count == 1);

    TEST("workflow_find_transition finds by from+event");
    WorkflowTransition *t = workflow_find_transition(&wf, "s1", "go");
    CHECK(t != NULL);
    CHECK(strcmp(t->target_state, "s2") == 0);

    TEST("workflow_find_transition returns NULL for mismatch");
    t = workflow_find_transition(&wf, "s1", "navigate");
    CHECK(t == NULL);
}

static int test_action_fn(void *ctx, void *data) {
    int *counter = (int *)ctx;
    (*counter)++;
    return 0;
}

static void test_workflow_instance(void) {
    TEST("workflow_instance_create initializes instance");
    WorkflowDefinition wf = workflow_define("InstWF");
    workflow_add_state(&wf, "alpha", "Alpha", true, false);
    workflow_add_state(&wf, "beta", "Beta", false, false);
    workflow_add_state(&wf, "omega", "Omega", false, true);
    workflow_add_transition(&wf, "alpha", "beta", "advance");
    workflow_add_transition(&wf, "beta", "omega", "finish");

    int data_val = 42;
    WorkflowInstance inst = workflow_instance_create(
        &wf, "inst-1", &data_val, sizeof(data_val));
    CHECK(strcmp(inst.current_state, "alpha") == 0);
    CHECK(!inst.is_completed);

    TEST("workflow_instance_send_event transitions state");
    int rc = workflow_instance_send_event(&inst, &wf, "advance");
    CHECK(rc == 0);
    CHECK(strcmp(inst.current_state, "beta") == 0);

    TEST("workflow_instance_send_event to final state");
    rc = workflow_instance_send_event(&inst, &wf, "finish");
    CHECK(rc == 0);
    CHECK(inst.is_completed);

    TEST("workflow_instance_send_event on completed returns -1");
    rc = workflow_instance_send_event(&inst, &wf, "advance");
    CHECK(rc == -1);

    workflow_instance_destroy(&inst);
}

static void test_workflow_actions(void) {
    TEST("entry actions execute on state entry");
    WorkflowDefinition wf = workflow_define("ActionWF");
    workflow_add_state(&wf, "start", "Start", true, false);
    workflow_add_state(&wf, "end", "End", false, true);
    workflow_add_transition(&wf, "start", "end", "go");

    int entry_count = 0;
    workflow_add_state_entry_action(&wf, "end", test_action_fn, &entry_count);

    int exit_count = 0;
    workflow_add_state_exit_action(&wf, "start", test_action_fn, &exit_count);

    int data = 0;
    WorkflowInstance inst = workflow_instance_create(
        &wf, "act-inst", &data, sizeof(data));
    int rc = workflow_instance_send_event(&inst, &wf, "go");
    CHECK(rc == 0);
    CHECK(exit_count >= 1);
    CHECK(entry_count >= 1);

    workflow_instance_destroy(&inst);
}

static void test_approval_workflow(void) {
    TEST("workflow_create_approval_workflow creates workflow");
    WorkflowDefinition wf = workflow_create_approval_workflow();
    CHECK(wf.state_count >= 4);
    CHECK(wf.transition_count >= 4);

    TEST("approval submit-approve cycle works");
    ApprovalRequestData req;
    memset(&req, 0, sizeof(req));
    strcpy(req.request_id, "REQ-TEST");
    req.state = APPROVAL_DRAFT;

    WorkflowInstance inst = workflow_instance_create(
        &wf, "test-appr", &req, sizeof(req));
    CHECK(strcmp(inst.current_state, "draft") == 0);

    int rc = approval_request_submit(&inst, &wf);
    CHECK(rc == 0);
    CHECK(strcmp(inst.current_state, "pending_approval") == 0);

    rc = approval_request_approve(&inst, &wf);
    CHECK(rc == 0);
    CHECK(inst.is_completed);

    workflow_instance_destroy(&inst);
}

static void test_guarded_transitions(void) {
    TEST("guarded transition evaluates guard");
    WorkflowDefinition wf = workflow_create_approval_workflow();

    static bool guard_passes = true;
    bool test_guard(void *ctx, void *data) {
        (void)ctx; (void)data;
        return guard_passes;
    }

    workflow_add_state(&wf, "fast_lane", "Fast Lane", false, true);
    workflow_add_guarded_transition(&wf, "pending_approval",
        "fast_lane", "fast_track", test_guard, NULL, "test");

    ApprovalRequestData req;
    memset(&req, 0, sizeof(req));
    req.state = APPROVAL_DRAFT;
    guard_passes = true;

    WorkflowInstance inst = workflow_instance_create(
        &wf, "guard-test", &req, sizeof(req));
    approval_request_submit(&inst, &wf);

    int rc = workflow_instance_send_event(&inst, &wf, "fast_track");
    CHECK(rc == 0);
    CHECK(strcmp(inst.current_state, "fast_lane") == 0);

    guard_passes = false;
    workflow_instance_destroy(&inst);

    WorkflowInstance inst2 = workflow_instance_create(
        &wf, "guard-fail", &req, sizeof(req));
    approval_request_submit(&inst2, &wf);
    rc = workflow_instance_send_event(&inst2, &wf, "fast_track");
    CHECK(rc == -3);

    workflow_instance_destroy(&inst2);
}

int main(void) {
    printf("=== Test: Workflow Engine ===\n\n");
    test_workflow_definition();
    test_workflow_transitions();
    test_workflow_instance();
    test_workflow_actions();
    test_approval_workflow();
    test_guarded_transitions();
    printf("\nResult: %d failures\n", failures);
    return failures;
}
