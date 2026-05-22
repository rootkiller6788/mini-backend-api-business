#include "bpmn_model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

BPMNNode bpmn_node_create(const char *id, const char *name, BPMNNodeType type) {
    BPMNNode node;
    memset(&node, 0, sizeof(node));
    strncpy(node.id, id, BPMN_ID_LEN);
    node.id[BPMN_ID_LEN - 1] = '\0';
    strncpy(node.name, name ? name : id, BPMN_NAME_LEN);
    node.name[BPMN_NAME_LEN - 1] = '\0';
    node.type = type;
    node.incoming_capacity = 4;
    node.incoming_flows = (BPMNSequenceFlow *)malloc(
        node.incoming_capacity * sizeof(BPMNSequenceFlow));
    node.incoming_count = 0;
    node.outgoing_capacity = 4;
    node.outgoing_flows = (BPMNSequenceFlow *)malloc(
        node.outgoing_capacity * sizeof(BPMNSequenceFlow));
    node.outgoing_count = 0;
    return node;
}

int bpmn_node_add_incoming(BPMNNode *node, BPMNSequenceFlow flow) {
    if (node->incoming_count >= node->incoming_capacity) {
        node->incoming_capacity *= 2;
        node->incoming_flows = (BPMNSequenceFlow *)realloc(
            node->incoming_flows,
            node->incoming_capacity * sizeof(BPMNSequenceFlow));
    }
    node->incoming_flows[node->incoming_count++] = flow;
    return 0;
}

int bpmn_node_add_outgoing(BPMNNode *node, BPMNSequenceFlow flow) {
    if (node->outgoing_count >= node->outgoing_capacity) {
        node->outgoing_capacity *= 2;
        node->outgoing_flows = (BPMNSequenceFlow *)realloc(
            node->outgoing_flows,
            node->outgoing_capacity * sizeof(BPMNSequenceFlow));
    }
    node->outgoing_flows[node->outgoing_count++] = flow;
    return 0;
}

void bpmn_node_destroy(BPMNNode *node) {
    free(node->incoming_flows);
    free(node->outgoing_flows);
}

BPMNSequenceFlow bpmn_flow_create(const char *id, const char *source,
    const char *target, const char *name) {
    BPMNSequenceFlow flow;
    memset(&flow, 0, sizeof(flow));
    strncpy(flow.id, id, BPMN_ID_LEN);
    flow.id[BPMN_ID_LEN - 1] = '\0';
    strncpy(flow.source_node_id, source, BPMN_ID_LEN);
    flow.source_node_id[BPMN_ID_LEN - 1] = '\0';
    strncpy(flow.target_node_id, target, BPMN_ID_LEN);
    flow.target_node_id[BPMN_ID_LEN - 1] = '\0';
    if (name) {
        strncpy(flow.name, name, BPMN_NAME_LEN);
        flow.name[BPMN_NAME_LEN - 1] = '\0';
    }
    flow.is_default = false;
    flow.evaluation_order = 0;
    return flow;
}

BPMNSequenceFlow bpmn_flow_create_conditional(const char *id,
    const char *source, const char *target, const char *condition,
    int order, bool is_default) {
    BPMNSequenceFlow flow = bpmn_flow_create(id, source, target, "");
    strncpy(flow.condition_expression, condition, DDD_DESC_LEN / 2);
    flow.condition_expression[(DDD_DESC_LEN / 2) - 1] = '\0';
    flow.evaluation_order = order;
    flow.is_default = is_default;
    return flow;
}

const char *bpmn_node_type_string(BPMNNodeType type) {
    switch (type) {
        case BPMN_NODE_START_EVENT:        return "START_EVENT";
        case BPMN_NODE_END_EVENT:          return "END_EVENT";
        case BPMN_NODE_USER_TASK:          return "USER_TASK";
        case BPMN_NODE_SERVICE_TASK:       return "SERVICE_TASK";
        case BPMN_NODE_SCRIPT_TASK:        return "SCRIPT_TASK";
        case BPMN_NODE_EXCLUSIVE_GATEWAY:  return "EXCLUSIVE_GATEWAY";
        case BPMN_NODE_PARALLEL_GATEWAY:   return "PARALLEL_GATEWAY";
        case BPMN_NODE_INCLUSIVE_GATEWAY:  return "INCLUSIVE_GATEWAY";
        case BPMN_NODE_INTERMEDIATE_TIMER: return "INTERMEDIATE_TIMER";
        case BPMN_NODE_SUB_PROCESS:        return "SUB_PROCESS";
        default:                           return "UNKNOWN";
    }
}

BPMNProcess bpmn_process_create(const char *id, const char *name) {
    BPMNProcess process;
    memset(&process, 0, sizeof(process));
    strncpy(process.id, id, BPMN_ID_LEN);
    process.id[BPMN_ID_LEN - 1] = '\0';
    strncpy(process.name, name, BPMN_NAME_LEN);
    process.name[BPMN_NAME_LEN - 1] = '\0';
    process.node_count = 0;
    process.flow_count = 0;
    process.instance_count = 0;
    process.start_node_id[0] = '\0';
    return process;
}

int bpmn_process_add_node(BPMNProcess *process, BPMNNode node) {
    if (process->node_count >= BPMN_MAX_NODES) return -1;
    process->nodes[process->node_count++] = node;
    return 0;
}

int bpmn_process_add_flow(BPMNProcess *process, BPMNSequenceFlow flow) {
    if (process->flow_count >= BPMN_MAX_FLOWS) return -1;
    process->flows[process->flow_count++] = flow;
    BPMNNode *src = bpmn_process_find_node(process, flow.source_node_id);
    BPMNNode *dst = bpmn_process_find_node(process, flow.target_node_id);
    if (src) bpmn_node_add_outgoing(src, flow);
    if (dst) bpmn_node_add_incoming(dst, flow);
    return 0;
}

int bpmn_process_set_start(BPMNProcess *process, const char *node_id) {
    strncpy(process->start_node_id, node_id, BPMN_ID_LEN);
    process->start_node_id[BPMN_ID_LEN - 1] = '\0';
    return 0;
}

int bpmn_process_validate(BPMNProcess *process) {
    if (process->start_node_id[0] == '\0') return -1;
    if (process->node_count == 0) return -2;
    if (bpmn_process_find_node(process, process->start_node_id) == NULL)
        return -3;
    bool has_end = false;
    for (int i = 0; i < process->node_count; i++) {
        if (process->nodes[i].type == BPMN_NODE_END_EVENT) {
            has_end = true;
            break;
        }
    }
    if (!has_end) return -4;
    return 0;
}

BPMNNode *bpmn_process_find_node(BPMNProcess *process, const char *id) {
    for (int i = 0; i < process->node_count; i++) {
        if (strcmp(process->nodes[i].id, id) == 0) {
            return &process->nodes[i];
        }
    }
    return NULL;
}

BPMNNode *bpmn_process_find_node_by_type(BPMNProcess *process,
    BPMNNodeType type) {
    for (int i = 0; i < process->node_count; i++) {
        if (process->nodes[i].type == type) {
            return &process->nodes[i];
        }
    }
    return NULL;
}

void bpmn_process_destroy(BPMNProcess *process) {
    for (int i = 0; i < process->node_count; i++) {
        bpmn_node_destroy(&process->nodes[i]);
    }
}

BPMNProcessInstance *bpmn_process_start(BPMNProcess *process,
    const char *instance_id, void *data, size_t data_size) {
    if (process->instance_count >= BPMN_MAX_INSTANCES) return NULL;
    BPMNProcessInstance *inst = &process->instances[process->instance_count];
    memset(inst, 0, sizeof(BPMNProcessInstance));
    snprintf(inst->process_id, BPMN_ID_LEN, "%s", process->id);
    snprintf(inst->instance_id, BPMN_ID_LEN, "%s",
        instance_id ? instance_id : "default-instance");
    strncpy(inst->current_state, "STARTED", 32);
    inst->process_data = malloc(data_size);
    memcpy(inst->process_data, data, data_size);
    inst->data_size = data_size;
    inst->is_completed = false;
    inst->is_terminated = false;
    inst->started_at_ms = (uint64_t)time(NULL) * 1000;

    BPMNNode *start_node = bpmn_process_find_node_by_type(
        process, BPMN_NODE_START_EVENT);
    if (!start_node) {
        start_node = bpmn_process_find_node(process, process->start_node_id);
    }
    if (start_node) {
        BPMNToken *tok = bpmn_token_create(start_node->id);
        tok->next = inst->tokens;
        inst->tokens = tok;
        inst->token_count = 1;
    }

    process->instance_count++;
    return inst;
}

static int execute_node(BPMNProcess *process, BPMNProcessInstance *instance,
    BPMNToken *token) {
    BPMNNode *node = bpmn_process_find_node(process, token->node_id);
    if (!node) return -1;

    switch (node->type) {
        case BPMN_NODE_START_EVENT:
        case BPMN_NODE_END_EVENT: {
            if (node->outgoing_count > 0) {
                strncpy(token->node_id,
                    node->outgoing_flows[0].target_node_id, BPMN_ID_LEN);
            } else {
                instance->is_completed = true;
            }
            return 0;
        }

        case BPMN_NODE_USER_TASK:
        case BPMN_NODE_SERVICE_TASK: {
            if (node->service_fn) {
                node->service_fn(node->service_context,
                    instance->process_data);
            }
            if (node->outgoing_count > 0) {
                strncpy(token->node_id,
                    node->outgoing_flows[0].target_node_id, BPMN_ID_LEN);
            }
            return 0;
        }

        case BPMN_NODE_SCRIPT_TASK: {
            if (node->script_fn) {
                node->script_fn(node->script_context,
                    instance->process_data);
            }
            if (node->outgoing_count > 0) {
                strncpy(token->node_id,
                    node->outgoing_flows[0].target_node_id, BPMN_ID_LEN);
            }
            return 0;
        }

        case BPMN_NODE_EXCLUSIVE_GATEWAY: {
            BPMNSequenceFlow *selected = NULL;
            bpmn_evaluate_gateway_exclusive(node, instance->process_data,
                &selected);
            if (selected) {
                strncpy(token->node_id, selected->target_node_id,
                    BPMN_ID_LEN);
            } else if (node->outgoing_count > 0) {
                for (int i = 0; i < node->outgoing_count; i++) {
                    if (node->outgoing_flows[i].is_default) {
                        strncpy(token->node_id,
                            node->outgoing_flows[i].target_node_id,
                            BPMN_ID_LEN);
                        break;
                    }
                }
            }
            return 0;
        }

        case BPMN_NODE_PARALLEL_GATEWAY: {
            BPMNSequenceFlow **flows = NULL;
            int flow_count = 0;
            bpmn_evaluate_gateway_parallel(node, instance->process_data,
                &flows, &flow_count);
            if (flow_count > 0 && flows) {
                strncpy(token->node_id, flows[0]->target_node_id,
                    BPMN_ID_LEN);
                for (int i = 1; i < flow_count; i++) {
                    BPMNToken *new_tok = bpmn_token_create(
                        flows[i]->target_node_id);
                    new_tok->next = instance->tokens;
                    instance->tokens = new_tok;
                    instance->token_count++;
                }
            }
            free(flows);
            return 0;
        }

        case BPMN_NODE_INCLUSIVE_GATEWAY: {
            BPMNSequenceFlow **iflows = NULL;
            int icount = 0;
            bpmn_evaluate_gateway_inclusive(node, instance->process_data,
                &iflows, &icount);
            if (icount > 0 && iflows) {
                strncpy(token->node_id, iflows[0]->target_node_id,
                    BPMN_ID_LEN);
                for (int i = 1; i < icount; i++) {
                    BPMNToken *new_tok = bpmn_token_create(
                        iflows[i]->target_node_id);
                    new_tok->next = instance->tokens;
                    instance->tokens = new_tok;
                    instance->token_count++;
                }
            }
            free(iflows);
            return 0;
        }

        default:
            return 0;
    }
}

int bpmn_process_execute_step(BPMNProcess *process,
    BPMNProcessInstance *instance) {
    if (instance->is_completed || instance->is_terminated) return -1;

    BPMNToken *token = instance->tokens;
    while (token) {
        if (token->active) {
            int rc = execute_node(process, instance, token);
            if (rc != 0) return rc;
            BPMNNode *node = bpmn_process_find_node(
                process, token->node_id);
            if (node && node->type == BPMN_NODE_END_EVENT) {
                token->active = false;
            }
        }
        token = token->next;
    }

    int active_count = 0;
    token = instance->tokens;
    while (token) {
        if (token->active) active_count++;
        token = token->next;
    }
    if (active_count == 0 && instance->token_count > 0) {
        instance->is_completed = true;
        instance->completed_at_ms = (uint64_t)time(NULL) * 1000;
    }

    return 0;
}

int bpmn_process_run(BPMNProcess *process, BPMNProcessInstance *instance,
    int max_steps) {
    for (int i = 0; i < max_steps; i++) {
        int rc = bpmn_process_execute_step(process, instance);
        if (rc != 0) return rc;
        if (instance->is_completed || instance->is_terminated) break;
    }
    return 0;
}

int bpmn_process_terminate(BPMNProcess *process,
    BPMNProcessInstance *instance) {
    (void)process;
    instance->is_terminated = true;
    instance->is_completed = true;
    return 0;
}

BPMNToken *bpmn_token_create(const char *node_id) {
    BPMNToken *token = (BPMNToken *)malloc(sizeof(BPMNToken));
    memset(token, 0, sizeof(BPMNToken));
    snprintf(token->token_id, BPMN_ID_LEN, "TOK-%010llu",
        (unsigned long long)time(NULL));
    strncpy(token->node_id, node_id, BPMN_ID_LEN);
    token->node_id[BPMN_ID_LEN - 1] = '\0';
    token->active = true;
    token->arrived_at_ms = (uint64_t)time(NULL) * 1000;
    return token;
}

void bpmn_token_destroy(BPMNToken *token) {
    free(token);
}

void bpmn_token_destroy_all(BPMNToken *head) {
    while (head) {
        BPMNToken *next = head->next;
        bpmn_token_destroy(head);
        head = next;
    }
}

BPMNToken *bpmn_token_move(BPMNToken *token, const char *new_node_id) {
    strncpy(token->node_id, new_node_id, BPMN_ID_LEN);
    token->node_id[BPMN_ID_LEN - 1] = '\0';
    token->arrived_at_ms = (uint64_t)time(NULL) * 1000;
    return token;
}

int bpmn_token_split(BPMNToken *source, BPMNToken **out_tokens,
    const char **target_node_ids, int target_count) {
    for (int i = 0; i < target_count; i++) {
        out_tokens[i] = bpmn_token_create(target_node_ids[i]);
    }
    return target_count;
}

int bpmn_evaluate_gateway_exclusive(BPMNNode *gateway,
    void *process_data, BPMNSequenceFlow **selected_flow) {
    (void)process_data;
    *selected_flow = NULL;
    for (int i = 0; i < gateway->outgoing_count; i++) {
        BPMNSequenceFlow *flow = &gateway->outgoing_flows[i];
        if (flow->is_default) {
            *selected_flow = flow;
            return 0;
        }
        if (flow->condition_expression[0] != '\0') {
            *selected_flow = flow;
            return 0;
        }
    }
    if (gateway->outgoing_count > 0 && !*selected_flow) {
        *selected_flow = &gateway->outgoing_flows[0];
    }
    return 0;
}

int bpmn_evaluate_gateway_parallel(BPMNNode *gateway,
    void *process_data, BPMNSequenceFlow ***out_flows, int *out_count) {
    (void)process_data;
    *out_count = gateway->outgoing_count;
    *out_flows = (BPMNSequenceFlow **)malloc(
        (*out_count) * sizeof(BPMNSequenceFlow *));
    for (int i = 0; i < *out_count; i++) {
        (*out_flows)[i] = &gateway->outgoing_flows[i];
    }
    return 0;
}

int bpmn_evaluate_gateway_inclusive(BPMNNode *gateway,
    void *process_data, BPMNSequenceFlow ***out_flows, int *out_count) {
    (void)process_data;
    int count = 0;
    for (int i = 0; i < gateway->outgoing_count; i++) {
        if (gateway->outgoing_flows[i].condition_expression[0] != '\0' ||
            gateway->outgoing_flows[i].is_default) {
            count++;
        }
    }
    if (count == 0) count = gateway->outgoing_count;
    *out_count = count;
    *out_flows = (BPMNSequenceFlow **)malloc(count * sizeof(BPMNSequenceFlow *));
    int idx = 0;
    for (int i = 0; i < gateway->outgoing_count && idx < count; i++) {
        (*out_flows)[idx++] = &gateway->outgoing_flows[i];
    }
    return 0;
}

static int order_fulfillment_service_task(void *ctx, void *data) {
    (void)ctx;
    (void)data;
    printf("[BPMN] OrderFulfillment: processing order\n");
    return 0;
}

BPMNProcess bpmn_create_order_fulfillment_process(void) {
    BPMNProcess p = bpmn_process_create("OrderFulfillment",
        "Order Fulfillment Process");

    BPMNNode start   = bpmn_node_create("start_of", "Start",
        BPMN_NODE_START_EVENT);
    BPMNNode validate = bpmn_node_create("validate_order", "Validate Order",
        BPMN_NODE_SERVICE_TASK);
    validate.service_fn = order_fulfillment_service_task;
    BPMNNode check_inv = bpmn_node_create("check_inventory",
        "Check Inventory", BPMN_NODE_SERVICE_TASK);
    check_inv.service_fn = order_fulfillment_service_task;
    BPMNNode gateway  = bpmn_node_create("gw_instock", "In Stock?",
        BPMN_NODE_EXCLUSIVE_GATEWAY);
    BPMNNode ship     = bpmn_node_create("ship_order", "Ship Order",
        BPMN_NODE_SERVICE_TASK);
    ship.service_fn = order_fulfillment_service_task;
    BPMNNode backorder = bpmn_node_create("backorder", "Backorder",
        BPMN_NODE_SERVICE_TASK);
    backorder.service_fn = order_fulfillment_service_task;
    BPMNNode notify   = bpmn_node_create("notify_customer",
        "Notify Customer", BPMN_NODE_SERVICE_TASK);
    notify.service_fn = order_fulfillment_service_task;
    BPMNNode end_of   = bpmn_node_create("end_of", "End",
        BPMN_NODE_END_EVENT);

    bpmn_process_add_node(&p, start);
    bpmn_process_add_node(&p, validate);
    bpmn_process_add_node(&p, check_inv);
    bpmn_process_add_node(&p, gateway);
    bpmn_process_add_node(&p, ship);
    bpmn_process_add_node(&p, backorder);
    bpmn_process_add_node(&p, notify);
    bpmn_process_add_node(&p, end_of);

    bpmn_process_add_flow(&p, bpmn_flow_create("f1", "start_of",
        "validate_order", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create("f2", "validate_order",
        "check_inventory", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create("f3", "check_inventory",
        "gw_instock", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create_conditional("f4", "gw_instock",
        "ship_order", "inventory > 0", 1, false));
    bpmn_process_add_flow(&p, bpmn_flow_create_conditional("f5", "gw_instock",
        "backorder", "", 2, true));
    bpmn_process_add_flow(&p, bpmn_flow_create("f6", "ship_order",
        "notify_customer", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create("f7", "backorder",
        "notify_customer", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create("f8", "notify_customer",
        "end_of", ""));

    bpmn_process_set_start(&p, "start_of");
    return p;
}

BPMNProcess bpmn_create_payment_processing_process(void) {
    BPMNProcess p = bpmn_process_create("PaymentProcessing",
        "Payment Processing Process");

    BPMNNode start     = bpmn_node_create("start_pp", "Start",
        BPMN_NODE_START_EVENT);
    BPMNNode authorize = bpmn_node_create("authorize_payment",
        "Authorize Payment", BPMN_NODE_SERVICE_TASK);
    BPMNNode capture   = bpmn_node_create("capture_payment",
        "Capture Payment", BPMN_NODE_SERVICE_TASK);
    BPMNNode refund_gw = bpmn_node_create("gw_refund", "Refund?",
        BPMN_NODE_EXCLUSIVE_GATEWAY);
    BPMNNode refund    = bpmn_node_create("refund_payment",
        "Refund Payment", BPMN_NODE_SERVICE_TASK);
    BPMNNode end_pp    = bpmn_node_create("end_pp", "End",
        BPMN_NODE_END_EVENT);

    bpmn_process_add_node(&p, start);
    bpmn_process_add_node(&p, authorize);
    bpmn_process_add_node(&p, capture);
    bpmn_process_add_node(&p, refund_gw);
    bpmn_process_add_node(&p, refund);
    bpmn_process_add_node(&p, end_pp);

    bpmn_process_add_flow(&p, bpmn_flow_create("pf1", "start_pp",
        "authorize_payment", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create("pf2", "authorize_payment",
        "capture_payment", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create("pf3", "capture_payment",
        "gw_refund", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create_conditional("pf4", "gw_refund",
        "refund_payment", "needs_refund == true", 1, false));
    bpmn_process_add_flow(&p, bpmn_flow_create_conditional("pf5", "gw_refund",
        "end_pp", "", 2, true));
    bpmn_process_add_flow(&p, bpmn_flow_create("pf6", "refund_payment",
        "end_pp", ""));

    bpmn_process_set_start(&p, "start_pp");
    return p;
}

BPMNProcess bpmn_create_expense_approval_process(void) {
    BPMNProcess p = bpmn_process_create("ExpenseApproval",
        "Expense Approval Process");

    BPMNNode start     = bpmn_node_create("start_ea", "Submit Expense",
        BPMN_NODE_START_EVENT);
    BPMNNode review    = bpmn_node_create("review_expense",
        "Review Expense", BPMN_NODE_USER_TASK);
    BPMNNode amount_gw = bpmn_node_create("gw_amount", "Amount?",
        BPMN_NODE_EXCLUSIVE_GATEWAY);
    BPMNNode auto_app  = bpmn_node_create("auto_approve", "Auto Approve",
        BPMN_NODE_SERVICE_TASK);
    BPMNNode mgr_app   = bpmn_node_create("manager_approve",
        "Manager Approval", BPMN_NODE_USER_TASK);
    BPMNNode dir_app   = bpmn_node_create("director_approve",
        "Director Approval", BPMN_NODE_USER_TASK);
    BPMNNode notify_ea = bpmn_node_create("notify_expense",
        "Notify Result", BPMN_NODE_SERVICE_TASK);
    BPMNNode end_ea    = bpmn_node_create("end_ea", "End",
        BPMN_NODE_END_EVENT);

    bpmn_process_add_node(&p, start);
    bpmn_process_add_node(&p, review);
    bpmn_process_add_node(&p, amount_gw);
    bpmn_process_add_node(&p, auto_app);
    bpmn_process_add_node(&p, mgr_app);
    bpmn_process_add_node(&p, dir_app);
    bpmn_process_add_node(&p, notify_ea);
    bpmn_process_add_node(&p, end_ea);

    bpmn_process_add_flow(&p, bpmn_flow_create("e1", "start_ea",
        "review_expense", "Submit"));
    bpmn_process_add_flow(&p, bpmn_flow_create("e2", "review_expense",
        "gw_amount", "Reviewed"));
    bpmn_process_add_flow(&p, bpmn_flow_create_conditional("e3", "gw_amount",
        "auto_approve", "amount < 100", 1, false));
    bpmn_process_add_flow(&p, bpmn_flow_create_conditional("e4", "gw_amount",
        "manager_approve", "amount < 1000", 2, false));
    bpmn_process_add_flow(&p, bpmn_flow_create_conditional("e5", "gw_amount",
        "director_approve", "", 3, true));
    bpmn_process_add_flow(&p, bpmn_flow_create("e6", "auto_approve",
        "notify_expense", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create("e7", "manager_approve",
        "notify_expense", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create("e8", "director_approve",
        "notify_expense", ""));
    bpmn_process_add_flow(&p, bpmn_flow_create("e9", "notify_expense",
        "end_ea", ""));

    bpmn_process_set_start(&p, "start_ea");
    return p;
}
