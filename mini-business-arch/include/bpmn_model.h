#ifndef BPMN_MODEL_H
#define BPMN_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ddd_model.h"

#define BPMN_ID_LEN         64
#define BPMN_NAME_LEN      128
#define BPMN_MAX_NODES     128
#define BPMN_MAX_FLOWS     256
#define BPMN_MAX_TOKENS     64
#define BPMN_MAX_INSTANCES  32

typedef enum {
    BPMN_NODE_START_EVENT = 0,
    BPMN_NODE_END_EVENT,
    BPMN_NODE_USER_TASK,
    BPMN_NODE_SERVICE_TASK,
    BPMN_NODE_SCRIPT_TASK,
    BPMN_NODE_EXCLUSIVE_GATEWAY,
    BPMN_NODE_PARALLEL_GATEWAY,
    BPMN_NODE_INCLUSIVE_GATEWAY,
    BPMN_NODE_INTERMEDIATE_TIMER,
    BPMN_NODE_SUB_PROCESS
} BPMNNodeType;

typedef struct BPMNNode BPMNNode;

typedef struct {
    char    id[BPMN_ID_LEN];
    char    source_node_id[BPMN_ID_LEN];
    char    target_node_id[BPMN_ID_LEN];
    char    name[BPMN_NAME_LEN];
    char    condition_expression[DDD_DESC_LEN / 2];
    bool    is_default;
    int     evaluation_order;
} BPMNSequenceFlow;

typedef int  (*bpmn_service_task_fn)(void *ctx, void *process_data);
typedef int  (*bpmn_script_fn)(void *ctx, void *process_data);

struct BPMNNode {
    char         id[BPMN_ID_LEN];
    char         name[BPMN_NAME_LEN];
    BPMNNodeType type;

    BPMNSequenceFlow *incoming_flows;
    int               incoming_count;
    int               incoming_capacity;

    BPMNSequenceFlow *outgoing_flows;
    int               outgoing_count;
    int               outgoing_capacity;

    bpmn_service_task_fn service_fn;
    void                *service_context;

    bpmn_script_fn script_fn;
    void          *script_context;
    char           script_text[DDD_DESC_LEN];

    bool is_interrupting;
    char attached_to[BPMN_ID_LEN];
    char timer_duration[32];
};

typedef struct BPMNToken BPMNToken;

struct BPMNToken {
    char       token_id[BPMN_ID_LEN];
    char       node_id[BPMN_ID_LEN];
    bool       active;
    uint64_t   arrived_at_ms;
    BPMNToken *next;
};

typedef struct {
    char           process_id[BPMN_ID_LEN];
    char           instance_id[BPMN_ID_LEN];
    char           current_state[32];
    BPMNToken     *tokens;
    int            token_count;
    void          *process_data;
    size_t         data_size;
    bool           is_completed;
    bool           is_terminated;
    uint64_t       started_at_ms;
    uint64_t       completed_at_ms;
} BPMNProcessInstance;

typedef struct {
    char          id[BPMN_ID_LEN];
    char          name[BPMN_NAME_LEN];
    char          description[DDD_DESC_LEN];

    BPMNNode      nodes[BPMN_MAX_NODES];
    int           node_count;

    BPMNSequenceFlow flows[BPMN_MAX_FLOWS];
    int               flow_count;

    char           start_node_id[BPMN_ID_LEN];
    BPMNProcessInstance instances[BPMN_MAX_INSTANCES];
    int                   instance_count;
} BPMNProcess;

BPMNNode      bpmn_node_create(const char *id, const char *name,
    BPMNNodeType type);
int           bpmn_node_add_incoming(BPMNNode *node, BPMNSequenceFlow flow);
int           bpmn_node_add_outgoing(BPMNNode *node, BPMNSequenceFlow flow);
void          bpmn_node_destroy(BPMNNode *node);

BPMNSequenceFlow bpmn_flow_create(const char *id, const char *source,
    const char *target, const char *name);
BPMNSequenceFlow bpmn_flow_create_conditional(const char *id,
    const char *source, const char *target, const char *condition,
    int order, bool is_default);
const char      *bpmn_node_type_string(BPMNNodeType type);

BPMNProcess bpmn_process_create(const char *id, const char *name);
int         bpmn_process_add_node(BPMNProcess *process, BPMNNode node);
int         bpmn_process_add_flow(BPMNProcess *process, BPMNSequenceFlow flow);
int         bpmn_process_set_start(BPMNProcess *process, const char *node_id);
int         bpmn_process_validate(BPMNProcess *process);
BPMNNode   *bpmn_process_find_node(BPMNProcess *process, const char *id);
BPMNNode   *bpmn_process_find_node_by_type(BPMNProcess *process,
    BPMNNodeType type);
void        bpmn_process_destroy(BPMNProcess *process);

BPMNProcessInstance *bpmn_process_start(BPMNProcess *process,
    const char *instance_id, void *data, size_t data_size);
int bpmn_process_execute_step(BPMNProcess *process,
    BPMNProcessInstance *instance);
int bpmn_process_run(BPMNProcess *process, BPMNProcessInstance *instance,
    int max_steps);
int bpmn_process_terminate(BPMNProcess *process,
    BPMNProcessInstance *instance);

BPMNToken *bpmn_token_create(const char *node_id);
void       bpmn_token_destroy(BPMNToken *token);
void       bpmn_token_destroy_all(BPMNToken *head);
BPMNToken *bpmn_token_move(BPMNToken *token, const char *new_node_id);
int        bpmn_token_split(BPMNToken *source, BPMNToken **out_tokens,
    const char **target_node_ids, int target_count);

int bpmn_evaluate_gateway_exclusive(BPMNNode *gateway,
    void *process_data, BPMNSequenceFlow **selected_flow);
int bpmn_evaluate_gateway_parallel(BPMNNode *gateway,
    void *process_data, BPMNSequenceFlow ***out_flows, int *out_count);
int bpmn_evaluate_gateway_inclusive(BPMNNode *gateway,
    void *process_data, BPMNSequenceFlow ***out_flows, int *out_count);

BPMNProcess bpmn_create_order_fulfillment_process(void);
BPMNProcess bpmn_create_payment_processing_process(void);
BPMNProcess bpmn_create_expense_approval_process(void);

#endif
