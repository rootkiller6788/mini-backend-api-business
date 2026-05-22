#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bpmn_model.h"

static int notify_service(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    printf("  [SERVICE_TASK] Sending notification...\n");
    return 0;
}

static int validate_service(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    printf("  [SERVICE_TASK] Validating data...\n");
    return 0;
}

typedef struct {
    char order_id[64];
    double amount;
    bool in_stock;
    bool needs_refund;
} OrderProcessData;

static void demo_bpmn_basics(void) {
    printf("--- Demo 1: BPMN Basics ---\n\n");

    printf("Node type strings:\n");
    printf("  START_EVENT:        %s\n",
        bpmn_node_type_string(BPMN_NODE_START_EVENT));
    printf("  END_EVENT:          %s\n",
        bpmn_node_type_string(BPMN_NODE_END_EVENT));
    printf("  USER_TASK:          %s\n",
        bpmn_node_type_string(BPMN_NODE_USER_TASK));
    printf("  SERVICE_TASK:       %s\n",
        bpmn_node_type_string(BPMN_NODE_SERVICE_TASK));
    printf("  SCRIPT_TASK:        %s\n",
        bpmn_node_type_string(BPMN_NODE_SCRIPT_TASK));
    printf("  EXCLUSIVE_GATEWAY:  %s\n",
        bpmn_node_type_string(BPMN_NODE_EXCLUSIVE_GATEWAY));
    printf("  PARALLEL_GATEWAY:   %s\n",
        bpmn_node_type_string(BPMN_NODE_PARALLEL_GATEWAY));
    printf("  INCLUSIVE_GATEWAY:  %s\n",
        bpmn_node_type_string(BPMN_NODE_INCLUSIVE_GATEWAY));
    printf("  SUB_PROCESS:        %s\n",
        bpmn_node_type_string(BPMN_NODE_SUB_PROCESS));

    BPMNNode task = bpmn_node_create("task1", "My Task",
        BPMN_NODE_USER_TASK);
    printf("\nCreated node: id=%s name=%s type=%s\n",
        task.id, task.name, bpmn_node_type_string(task.type));

    BPMNSequenceFlow flow_in = bpmn_flow_create(
        "flow_in", "start", "task1", "Start->Task1");
    BPMNSequenceFlow flow_out = bpmn_flow_create(
        "flow_out", "task1", "end", "Task1->End");

    bpmn_node_add_incoming(&task, flow_in);
    bpmn_node_add_outgoing(&task, flow_out);
    printf("Task incoming: %d, outgoing: %d\n",
        task.incoming_count, task.outgoing_count);

    BPMNSequenceFlow cond_flow = bpmn_flow_create_conditional(
        "flow_cond", "gateway", "task2", "amount > 100", 1, false);
    printf("Conditional flow: id=%s condition=%s order=%d default=%s\n",
        cond_flow.id, cond_flow.condition_expression,
        cond_flow.evaluation_order,
        cond_flow.is_default ? "yes" : "no");

    bpmn_node_destroy(&task);
}

static void demo_order_fulfillment(void) {
    printf("\n\n--- Demo 2: Order Fulfillment Process ---\n\n");

    BPMNProcess process = bpmn_create_order_fulfillment_process();
    printf("Process: %s (%s)\n", process.name, process.id);
    printf("Nodes: %d, Flows: %d\n", process.node_count, process.flow_count);

    printf("\nNode list:\n");
    for (int i = 0; i < process.node_count; i++) {
        BPMNNode *n = &process.nodes[i];
        printf("  %-20s type=%-18s in=%d out=%d\n",
            n->id, bpmn_node_type_string(n->type),
            n->incoming_count, n->outgoing_count);
    }

    int valid_rc = bpmn_process_validate(&process);
    printf("\nValidation: %s (rc=%d)\n",
        valid_rc == 0 ? "PASSED" : "FAILED", valid_rc);

    OrderProcessData data;
    memset(&data, 0, sizeof(data));
    strncpy(data.order_id, "ORD-FULFILL-001", 64);
    data.amount = 150.00;
    data.in_stock = true;

    BPMNProcessInstance *inst = bpmn_process_start(
        &process, "inst-of-001", &data, sizeof(data));
    printf("\nInstance started: %s tokens=%d\n",
        inst->instance_id, inst->token_count);

    printf("Running process (max 10 steps)...\n");
    bpmn_process_run(&process, inst, 10);
    printf("Completed: %s tokens=%d\n",
        inst->is_completed ? "YES" : "NO", inst->token_count);

    bpmn_process_destroy(&process);
}

static void demo_payment_processing(void) {
    printf("\n\n--- Demo 3: Payment Processing Process ---\n\n");

    BPMNProcess process = bpmn_create_payment_processing_process();
    printf("Process: %s with %d nodes\n", process.name, process.node_count);

    BPMNNode *start = bpmn_process_find_node_by_type(
        &process, BPMN_NODE_START_EVENT);
    printf("Start event node: %s\n", start ? start->id : "NOT FOUND");

    BPMNNode *end = bpmn_process_find_node(&process, "end_pp");
    printf("End node: %s type=%s\n",
        end ? end->id : "NOT FOUND",
        end ? bpmn_node_type_string(end->type) : "N/A");

    OrderProcessData data;
    memset(&data, 0, sizeof(data));
    strncpy(data.order_id, "ORD-PAY-002", 64);
    data.amount = 250.00;
    data.needs_refund = false;

    BPMNProcessInstance *inst = bpmn_process_start(
        &process, "inst-pp-001", &data, sizeof(data));

    printf("Initial token at: %s\n",
        inst->tokens ? inst->tokens->node_id : "NULL");

    bpmn_process_run(&process, inst, 5);
    printf("After 5 steps: completed=%s\n",
        inst->is_completed ? "YES" : "NO");

    printf("Refund scenario...\n");
    OrderProcessData refund_data;
    memset(&refund_data, 0, sizeof(refund_data));
    strncpy(refund_data.order_id, "ORD-REFUND-003", 64);
    refund_data.needs_refund = true;

    BPMNProcessInstance *inst2 = bpmn_process_start(
        &process, "inst-pp-002", &refund_data, sizeof(refund_data));
    bpmn_process_run(&process, inst2, 5);
    printf("Refund instance completed: %s\n",
        inst2->is_completed ? "YES" : "NO");

    bpmn_process_destroy(&process);
}

static void demo_expense_approval(void) {
    printf("\n\n--- Demo 4: Expense Approval Process ---\n\n");

    BPMNProcess process = bpmn_create_expense_approval_process();
    printf("Process: %s with %d nodes, %d flows\n",
        process.name, process.node_count, process.flow_count);

    printf("Flow analysis:\n");
    for (int i = 0; i < process.flow_count; i++) {
        BPMNSequenceFlow *f = &process.flows[i];
        printf("  %-15s -> %-20s [%s] %s\n",
            f->source_node_id, f->target_node_id,
            f->id, f->condition_expression[0] ? f->condition_expression : "");
    }

    typedef struct {
        char   employee[64];
        double expense_amount;
    } ExpenseData;

    ExpenseData ed_low = {"alice", 75.00};
    BPMNProcessInstance *inst_low = bpmn_process_start(
        &process, "inst-ea-low", &ed_low, sizeof(ed_low));
    printf("\nLow amount ($%.2f) started\n", ed_low.expense_amount);
    bpmn_process_run(&process, inst_low, 10);
    printf("Low amount completed: %s\n",
        inst_low->is_completed ? "YES" : "NO");

    ExpenseData ed_mid = {"bob", 750.00};
    BPMNProcessInstance *inst_mid = bpmn_process_start(
        &process, "inst-ea-mid", &ed_mid, sizeof(ed_mid));
    printf("Mid amount ($%.2f) started\n", ed_mid.expense_amount);
    bpmn_process_run(&process, inst_mid, 10);
    printf("Mid amount completed: %s\n",
        inst_mid->is_completed ? "YES" : "NO");

    ExpenseData ed_high = {"charlie", 5000.00};
    BPMNProcessInstance *inst_high = bpmn_process_start(
        &process, "inst-ea-high", &ed_high, sizeof(ed_high));
    printf("High amount ($%.2f) started\n", ed_high.expense_amount);
    bpmn_process_run(&process, inst_high, 10);
    printf("High amount completed: %s\n",
        inst_high->is_completed ? "YES" : "NO");

    bpmn_process_destroy(&process);
}

static void demo_token_semantics(void) {
    printf("\n\n--- Demo 5: Token-Based Execution Semantics ---\n\n");

    BPMNToken *t1 = bpmn_token_create("node_a");
    BPMNToken *t2 = bpmn_token_create("node_b");
    BPMNToken *t3 = bpmn_token_create("node_c");

    t1->next = t2;
    t2->next = t3;

    printf("Token chain: ");
    for (BPMNToken *t = t1; t; t = t->next) {
        printf("[%s@%s] -> ",
            t->token_id, t->node_id);
    }
    printf("NULL\n");

    bpmn_token_move(t1, "node_destination");
    printf("After move: t1 is now at %s\n", t1->node_id);

    const char *targets[] = {"parallel_a", "parallel_b", "parallel_c"};
    BPMNToken *split_tokens[3];
    int count = bpmn_token_split(t1, split_tokens, targets, 3);
    printf("Split token into %d tokens:\n", count);
    for (int i = 0; i < count; i++) {
        printf("  [%d] id=%s node=%s\n",
            i, split_tokens[i]->token_id, split_tokens[i]->node_id);
        bpmn_token_destroy(split_tokens[i]);
    }

    BPMNProcess proc = bpmn_process_create("TokenTest", "Token Test");
    BPMNNode start = bpmn_node_create("start_t", "Start",
        BPMN_NODE_START_EVENT);
    BPMNNode para_gw = bpmn_node_create("para_gw", "Parallel Gateway",
        BPMN_NODE_PARALLEL_GATEWAY);
    BPMNNode task_a = bpmn_node_create("task_a", "Task A",
        BPMN_NODE_SERVICE_TASK);
    BPMNNode task_b = bpmn_node_create("task_b", "Task B",
        BPMN_NODE_SERVICE_TASK);
    BPMNNode end_t = bpmn_node_create("end_t", "End",
        BPMN_NODE_END_EVENT);

    bpmn_process_add_node(&proc, start);
    bpmn_process_add_node(&proc, para_gw);
    bpmn_process_add_node(&proc, task_a);
    bpmn_process_add_node(&proc, task_b);
    bpmn_process_add_node(&proc, end_t);

    bpmn_process_add_flow(&proc, bpmn_flow_create("t1", "start_t",
        "para_gw", ""));
    bpmn_process_add_flow(&proc, bpmn_flow_create("t2", "para_gw",
        "task_a", ""));
    bpmn_process_add_flow(&proc, bpmn_flow_create("t3", "para_gw",
        "task_b", ""));
    bpmn_process_add_flow(&proc, bpmn_flow_create("t4", "task_a",
        "end_t", ""));
    bpmn_process_add_flow(&proc, bpmn_flow_create("t5", "task_b",
        "end_t", ""));

    bpmn_process_set_start(&proc, "start_t");

    printf("\nParallel gateway process created:\n");
    printf("  Start node: %s\n", proc.start_node_id);
    printf("  Gateway outgoing: %d (twin: %zu should be 2)\n",
        para_gw.outgoing_count, (size_t)para_gw.outgoing_count);

    int dummy = 0;
    BPMNProcessInstance *inst = bpmn_process_start(
        &proc, "inst-tok", &dummy, sizeof(dummy));
    printf("  Initial token count: %d\n", inst->token_count);

    bpmn_process_execute_step(&proc, inst);
    printf("  After 1 step: token_count=%d tokens at:",
        inst->token_count);
    for (BPMNToken *t = inst->tokens; t; t = t->next) {
        printf(" %s", t->node_id);
    }
    printf("\n");

    bpmn_process_terminate(&proc, inst);
    printf("  Terminated: %s\n", inst->is_terminated ? "YES" : "NO");

    bpmn_process_destroy(&proc);
}

int main(void) {
    printf("=== BPMN Model Demos ===\n\n");

    demo_bpmn_basics();
    demo_order_fulfillment();
    demo_payment_processing();
    demo_expense_approval();
    demo_token_semantics();

    printf("\n=== All BPMN Demos Complete ===\n");
    return 0;
}
