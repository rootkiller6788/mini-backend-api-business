# API Reference

## DDD Model (`ddd_model.h`)

### Entity & Identity

| Function | Description |
|----------|-------------|
| `entity_id_generate()` | Generate unique EntityId |
| `entity_id_equals(a, b)` | Compare two EntityIds |
| `entity_id_string(id)` | Get string representation |

### Value Objects

| Function | Description |
|----------|-------------|
| `address_equals(a, b)` | Compare AddressValueObject |
| `address_copy(src)` | Create copy (immutable) |
| `money_equals(a, b)` | Compare MoneyValueObject |
| `money_add(a, b)` | Add two money values |
| `money_multiply(a, f)` | Multiply money by factor |
| `order_line_equals(a, b)` | Compare OrderLineValueObject |

### Order Aggregate

| Function | Description |
|----------|-------------|
| `order_aggregate_create(customer_id)` | Create new draft order |
| `order_aggregate_add_line_item(agg, ...)` | Add product line item |
| `order_aggregate_remove_line_item(agg, pid)` | Remove line item by product |
| `order_aggregate_place(agg, ship, bill)` | Place order (draft→placed) |
| `order_aggregate_ship(agg)` | Mark as shipped |
| `order_aggregate_cancel(agg)` | Cancel order |
| `order_aggregate_calculate_total(agg)` | Compute total money |
| `order_aggregate_destroy(agg)` | Free resources |

### Repository

| Function | Description |
|----------|-------------|
| `repository_create_in_memory()` | Create in-memory OrderRepository |

### Domain Service

| Function | Description |
|----------|-------------|
| `order_domain_service_create(repo)` | Create service |
| `order_domain_service_place_order(svc, order)` | Validate + persist |
| `order_domain_service_cancel_order(svc, order)` | Cancel + persist |
| `order_domain_service_check_inventory(svc, order, ...)` | Check availability |

## CQRS & Event Sourcing (`cqrs_es.h`)

### Events

| Function | Description |
|----------|-------------|
| `event_make(type, agg_id, ver, payload, size)` | Create event |
| `event_make_json(type, agg_id, ver, json)` | Create from JSON |
| `event_type_string(e)` | Get event type |
| `event_payload_string(e)` | Get payload |
| `event_sequence(e)` | Get global sequence |
| `event_is_type(e, type)` | Check event type |

### Command Bus

| Function | Description |
|----------|-------------|
| `command_bus_create(store)` | Create with EventStore |
| `command_bus_destroy(bus)` | Free bus |
| `command_bus_register_handler(bus, type, fn, ctx)` | Register handler |
| `command_bus_dispatch(bus, cmd, out_count)` | Dispatch command |

### Query Bus

| Function | Description |
|----------|-------------|
| `query_bus_create()` | Create bus |
| `query_bus_destroy(bus)` | Free bus |
| `query_bus_register_handler(bus, type, fn, ctx)` | Register handler |
| `query_bus_dispatch(bus, q)` | Dispatch query |

### Projections

| Function | Description |
|----------|-------------|
| `projection_engine_create(store)` | Create engine |
| `projection_engine_destroy(engine)` | Free engine |
| `projection_engine_register(engine, name, fn, ctx)` | Add projection |
| `projection_engine_replay_events(engine, agg_id)` | Rebuild read model |
| `projection_engine_poll(engine)` | Poll new events |

### Event Sourcing Helpers

| Function | Description |
|----------|-------------|
| `event_sourced_rebuild(agg, store, id, apply)` | Rebuild from events |
| `event_sourced_save_snapshot(snap, id, data, size, ver)` | Save snapshot |
| `event_sourced_load_from_snapshot(agg, store, id, snap, ...)` | Load + catch up |

## Saga Pattern (`saga_orch.h`)

### Saga Steps

| Function | Description |
|----------|-------------|
| `saga_step_create(name, exec, comp, ctx, retries)` | Create step |
| `saga_step_set_status(step, status)` | Set status |
| `saga_step_status_string(status)` | Get status string |
| `saga_step_can_retry(step)` | Check retry eligibility |

### Orchestration Saga

| Function | Description |
|----------|-------------|
| `saga_orch_create(name, orchestrated)` | Create saga instance |
| `saga_orch_add_step(saga, step)` | Add single step |
| `saga_orch_add_steps(saga, steps, count)` | Add multiple steps |
| `saga_orch_execute(saga)` | Execute all steps |
| `saga_orch_execute_step(saga, idx)` | Execute one step |
| `saga_orch_compensate(saga, failed_idx)` | Rollback completed steps |
| `saga_orch_resume(saga)` | Resume execution |
| `saga_status_string(status)` | Get status string |
| `saga_is_terminal(saga)` | Check if finished |

### Orchestrator

| Function | Description |
|----------|-------------|
| `saga_orchestrator_create()` | Create orchestrator |
| `saga_orchestrator_destroy(orch)` | Free orchestrator |
| `saga_orchestrator_enqueue(orch, saga)` | Enqueue saga |
| `saga_orchestrator_tick(orch)` | Process all active sagas |
| `saga_orchestrator_find(orch, id)` | Find by ID |
| `saga_orchestrator_cancel(orch, id)` | Cancel + compensate |

### Choreography

| Function | Description |
|----------|-------------|
| `choreography_bus_create()` | Create event bus |
| `choreography_bus_destroy(bus)` | Free bus |
| `choreography_bus_register_service(bus, svc)` | Register service |
| `choreography_bus_publish(bus, event)` | Publish event |
| `choreography_bus_dispatch_pending(bus)` | Dispatch to services |
| `ch_event_started/ completed/ failed(...)` | Event constructors |

### Built-in Saga

| Function | Description |
|----------|-------------|
| `saga_create_order_saga(oid, cid, amount, curr, orch)` | Create order saga |

## Workflow Engine (`workflow_engine.h`)

| Function | Description |
|----------|-------------|
| `workflow_define(name)` | Create definition |
| `workflow_add_state(def, name, display, init, final)` | Add state |
| `workflow_add_state_entry_action(def, state, fn, ctx)` | Entry action |
| `workflow_add_state_exit_action(def, state, fn, ctx)` | Exit action |
| `workflow_add_transition(def, from, to, event)` | Add transition |
| `workflow_add_guarded_transition(def, from, to, ev, guard, ctx, desc)` | Guarded |
| `workflow_add_transition_action(def, from, ev, fn, ctx)` | Transition action |
| `workflow_add_fork(def, from, targets, cnt, join)` | Fork branch |
| `workflow_add_join(def, join, to, event)` | Join branch |
| `workflow_add_timer(def, from, event, delay_ms)` | Timer transition |
| `workflow_instance_create(def, id, data, size)` | Create instance |
| `workflow_instance_send_event(inst, def, event)` | Trigger transition |
| `workflow_instance_tick_timers(inst, def, now_ms)` | Check timers |
| `workflow_find_state(def, name)` | Lookup state |
| `workflow_find_transition(def, from, event)` | Lookup transition |
| `workflow_initial_state(def)` | Get initial state |
| `workflow_is_final_state(def, name)` | Check if final |
| `workflow_instance_destroy(inst)` | Free instance |

### Approval Helpers

| Function | Description |
|----------|-------------|
| `workflow_create_approval_workflow()` | Create approval workflow |
| `approval_request_submit(inst, def)` | Submit |
| `approval_request_approve(inst, def)` | Approve |
| `approval_request_reject(inst, def)` | Reject |
| `approval_request_request_revision(inst, def)` | Request revision |
| `approval_request_resubmit(inst, def)` | Resubmit |

## BPMN Models (`bpmn_model.h`)

| Function | Description |
|----------|-------------|
| `bpmn_node_create(id, name, type)` | Create node |
| `bpmn_node_add_incoming(node, flow)` | Add incoming flow |
| `bpmn_node_add_outgoing(node, flow)` | Add outgoing flow |
| `bpmn_node_destroy(node)` | Free node |
| `bpmn_flow_create(id, src, tgt, name)` | Create flow |
| `bpmn_flow_create_conditional(id, src, tgt, cond, order, default)` | Conditional |
| `bpmn_node_type_string(type)` | Type to string |
| `bpmn_process_create(id, name)` | Create process |
| `bpmn_process_add_node(proc, node)` | Add node |
| `bpmn_process_add_flow(proc, flow)` | Add flow (wires nodes) |
| `bpmn_process_set_start(proc, node_id)` | Set start node |
| `bpmn_process_validate(proc)` | Validate process |
| `bpmn_process_find_node(proc, id)` | Find node by ID |
| `bpmn_process_find_node_by_type(proc, type)` | Find by type |
| `bpmn_process_destroy(proc)` | Free process |
| `bpmn_process_start(proc, inst_id, data, size)` | Start instance |
| `bpmn_process_execute_step(proc, inst)` | Execute one step |
| `bpmn_process_run(proc, inst, max_steps)` | Run N steps |
| `bpmn_process_terminate(proc, inst)` | Terminate instance |
| `bpmn_token_create(node_id)` | Create token |
| `bpmn_token_destroy(token)` | Free token |
| `bpmn_token_destroy_all(head)` | Free token chain |
| `bpmn_token_move(token, node_id)` | Move token |
| `bpmn_token_split(src, out, targets, cnt)` | Split token |
| `bpmn_evaluate_gateway_exclusive(gw, data, out)` | Exclusive gateway |
| `bpmn_evaluate_gateway_parallel(gw, data, out, cnt)` | Parallel gateway |
| `bpmn_evaluate_gateway_inclusive(gw, data, out, cnt)` | Inclusive gateway |
| `bpmn_create_order_fulfillment_process()` | Pre-built process |
| `bpmn_create_payment_processing_process()` | Pre-built process |
| `bpmn_create_expense_approval_process()` | Pre-built process |
