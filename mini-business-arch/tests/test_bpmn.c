#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "bpmn_model.h"

static int failures = 0;
#define TEST(name) printf("  %-55s", name)
#define CHECK(cond) do { \
    if (!(cond)) { printf(" FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else printf(" PASS\n"); \
} while(0)

static void test_bpmn_node(void) {
    TEST("bpmn_node_create initializes node");
    BPMNNode node = bpmn_node_create("n1", "Node 1", BPMN_NODE_USER_TASK);
    CHECK(strcmp(node.id, "n1") == 0);
    CHECK(strcmp(node.name, "Node 1") == 0);
    CHECK(node.type == BPMN_NODE_USER_TASK);
    CHECK(node.incoming_count == 0);
    CHECK(node.outgoing_count == 0);

    TEST("bpmn_node_type_string returns all types");
    CHECK(strcmp(bpmn_node_type_string(BPMN_NODE_START_EVENT), "START_EVENT") == 0);
    CHECK(strcmp(bpmn_node_type_string(BPMN_NODE_END_EVENT), "END_EVENT") == 0);
    CHECK(strcmp(bpmn_node_type_string(BPMN_NODE_SERVICE_TASK), "SERVICE_TASK") == 0);
    CHECK(strcmp(bpmn_node_type_string(BPMN_NODE_EXCLUSIVE_GATEWAY), "EXCLUSIVE_GATEWAY") == 0);
    CHECK(strcmp(bpmn_node_type_string(BPMN_NODE_PARALLEL_GATEWAY), "PARALLEL_GATEWAY") == 0);

    TEST("bpmn_node_add_incoming adds flow");
    BPMNSequenceFlow flow = bpmn_flow_create("f1", "src", "n1", "to-n1");
    int rc = bpmn_node_add_incoming(&node, flow);
    CHECK(rc == 0);
    CHECK(node.incoming_count == 1);

    TEST("bpmn_node_add_outgoing adds flow");
    BPMNSequenceFlow flow2 = bpmn_flow_create("f2", "n1", "dst", "from-n1");
    rc = bpmn_node_add_outgoing(&node, flow2);
    CHECK(rc == 0);
    CHECK(node.outgoing_count == 1);

    bpmn_node_destroy(&node);
}

static void test_bpmn_flow(void) {
    TEST("bpmn_flow_create creates sequence flow");
    BPMNSequenceFlow flow = bpmn_flow_create("sf1", "nodeA", "nodeB", "A->B");
    CHECK(strcmp(flow.id, "sf1") == 0);
    CHECK(strcmp(flow.source_node_id, "nodeA") == 0);
    CHECK(strcmp(flow.target_node_id, "nodeB") == 0);
    CHECK(!flow.is_default);

    TEST("bpmn_flow_create_conditional with condition");
    BPMNSequenceFlow cflow = bpmn_flow_create_conditional(
        "cf1", "gw", "task", "amount > 100", 1, false);
    CHECK(strlen(cflow.condition_expression) > 0);
    CHECK(cflow.evaluation_order == 1);
    CHECK(!cflow.is_default);

    TEST("bpmn_flow_create_conditional default flow");
    BPMNSequenceFlow dflow = bpmn_flow_create_conditional(
        "df1", "gw", "task", "", 5, true);
    CHECK(dflow.is_default);
}

static void test_bpmn_process(void) {
    TEST("bpmn_process_create initializes process");
    BPMNProcess proc = bpmn_process_create("TestProc", "Test Process");
    CHECK(strcmp(proc.id, "TestProc") == 0);
    CHECK(proc.node_count == 0);
    CHECK(proc.flow_count == 0);

    TEST("bpmn_process_add_node adds node");
    BPMNNode n1 = bpmn_node_create("n_start", "Start", BPMN_NODE_START_EVENT);
    BPMNNode n2 = bpmn_node_create("n_end", "End", BPMN_NODE_END_EVENT);
    int rc = bpmn_process_add_node(&proc, n1);
    CHECK(rc == 0);
    rc = bpmn_process_add_node(&proc, n2);
    CHECK(rc == 0);
    CHECK(proc.node_count == 2);

    TEST("bpmn_process_add_flow adds flow and wires nodes");
    BPMNSequenceFlow flow = bpmn_flow_create("main", "n_start", "n_end", "");
    rc = bpmn_process_add_flow(&proc, flow);
    CHECK(rc == 0);
    CHECK(proc.flow_count == 1);

    TEST("bpmn_process_find_node finds by ID");
    BPMNNode *found = bpmn_process_find_node(&proc, "n_start");
    CHECK(found != NULL);
    CHECK(found->type == BPMN_NODE_START_EVENT);

    TEST("bpmn_process_find_node returns NULL for unknown");
    found = bpmn_process_find_node(&proc, "nonexistent");
    CHECK(found == NULL);

    TEST("bpmn_process_find_node_by_type finds by type");
    found = bpmn_process_find_node_by_type(&proc, BPMN_NODE_END_EVENT);
    CHECK(found != NULL);
    CHECK(strcmp(found->id, "n_end") == 0);

    TEST("bpmn_process_set_start and validate");
    rc = bpmn_process_set_start(&proc, "n_start");
    CHECK(rc == 0);
    rc = bpmn_process_validate(&proc);
    CHECK(rc == 0);

    bpmn_process_destroy(&proc);
}

static void test_bpmn_execution(void) {
    TEST("bpmn_process_start creates instance with token");
    BPMNProcess proc = bpmn_process_create("ExecTest", "Execution Test");
    BPMNNode start = bpmn_node_create("s", "Start", BPMN_NODE_START_EVENT);
    BPMNNode end   = bpmn_node_create("e", "End", BPMN_NODE_END_EVENT);
    bpmn_process_add_node(&proc, start);
    bpmn_process_add_node(&proc, end);
    bpmn_process_add_flow(&proc, bpmn_flow_create("f", "s", "e", ""));
    bpmn_process_set_start(&proc, "s");

    int data = 123;
    BPMNProcessInstance *inst = bpmn_process_start(
        &proc, "inst-exec", &data, sizeof(data));
    CHECK(inst != NULL);
    CHECK(inst->token_count >= 1);

    TEST("bpmn_process_execute_step moves token");
    int rc = bpmn_process_execute_step(&proc, inst);
    CHECK(rc == 0 || rc == -1);

    TEST("bpmn_process_run executes to completion");
    BPMNProcessInstance *inst2 = bpmn_process_start(
        &proc, "inst-run", &data, sizeof(data));
    rc = bpmn_process_run(&proc, inst2, 10);
    CHECK(rc == 0);

    TEST("bpmn_process_terminate marks instance");
    BPMNProcessInstance *inst3 = bpmn_process_start(
        &proc, "inst-term", &data, sizeof(data));
    rc = bpmn_process_terminate(&proc, inst3);
    CHECK(rc == 0);
    CHECK(inst3->is_terminated);

    bpmn_process_destroy(&proc);
}

static void test_bpmn_token(void) {
    TEST("bpmn_token_create creates token");
    BPMNToken *tok = bpmn_token_create("node_x");
    CHECK(tok != NULL);
    CHECK(tok->active);
    CHECK(strcmp(tok->node_id, "node_x") == 0);

    TEST("bpmn_token_move changes position");
    bpmn_token_move(tok, "node_y");
    CHECK(strcmp(tok->node_id, "node_y") == 0);

    TEST("bpmn_token_split creates multiple tokens");
    const char *targets[] = {"t1", "t2"};
    BPMNToken *out[2];
    int count = bpmn_token_split(tok, out, targets, 2);
    CHECK(count == 2);
    bpmn_token_destroy(out[0]);
    bpmn_token_destroy(out[1]);

    bpmn_token_destroy(tok);
}

static void test_bpmn_gateways(void) {
    TEST("exclusive gateway selects first conditional flow");
    BPMNNode gw;
    memset(&gw, 0, sizeof(gw));
    gw.id[0] = 'g'; gw.id[1] = '\0';
    gw.type = BPMN_NODE_EXCLUSIVE_GATEWAY;
    gw.outgoing_capacity = 4;
    gw.outgoing_flows = (BPMNSequenceFlow *)malloc(4 * sizeof(BPMNSequenceFlow));
    gw.outgoing_count = 2;
    gw.outgoing_flows[0] = bpmn_flow_create_conditional("c1", "g", "t1",
        "cond=true", 1, false);
    gw.outgoing_flows[1] = bpmn_flow_create_conditional("c2", "g", "t2",
        "", 2, true);

    BPMNSequenceFlow *selected = NULL;
    int rc = bpmn_evaluate_gateway_exclusive(&gw, NULL, &selected);
    CHECK(rc == 0);
    CHECK(selected != NULL);

    free(gw.outgoing_flows);

    TEST("parallel gateway selects all outgoing");
    BPMNNode pgw;
    memset(&pgw, 0, sizeof(pgw));
    pgw.type = BPMN_NODE_PARALLEL_GATEWAY;
    pgw.outgoing_capacity = 3;
    pgw.outgoing_flows = (BPMNSequenceFlow *)malloc(3 * sizeof(BPMNSequenceFlow));
    pgw.outgoing_count = 3;
    for (int i = 0; i < 3; i++) {
        char id[16];
        snprintf(id, 16, "p%d", i);
        pgw.outgoing_flows[i] = bpmn_flow_create(id, "pg", "t", "p");
    }

    BPMNSequenceFlow **out_flows = NULL;
    int out_count = 0;
    rc = bpmn_evaluate_gateway_parallel(&pgw, NULL, &out_flows, &out_count);
    CHECK(rc == 0);
    CHECK(out_count == 3);
    free(out_flows);
    free(pgw.outgoing_flows);
}

static void test_prebuilt_processes(void) {
    TEST("order fulfillment process is valid");
    BPMNProcess proc = bpmn_create_order_fulfillment_process();
    int rc = bpmn_process_validate(&proc);
    CHECK(rc == 0);
    CHECK(proc.node_count > 0);
    CHECK(proc.flow_count > 0);
    bpmn_process_destroy(&proc);

    TEST("payment processing process is valid");
    proc = bpmn_create_payment_processing_process();
    rc = bpmn_process_validate(&proc);
    CHECK(rc == 0);
    bpmn_process_destroy(&proc);

    TEST("expense approval process is valid");
    proc = bpmn_create_expense_approval_process();
    rc = bpmn_process_validate(&proc);
    CHECK(rc == 0);
    bpmn_process_destroy(&proc);
}

int main(void) {
    printf("=== Test: BPMN Models ===\n\n");
    test_bpmn_node();
    test_bpmn_flow();
    test_bpmn_process();
    test_bpmn_execution();
    test_bpmn_token();
    test_bpmn_gateways();
    test_prebuilt_processes();
    printf("\nResult: %d failures\n", failures);
    return failures;
}
