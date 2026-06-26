# Knowledge Graph — mini-business-arch

## L1: Core Definitions (Complete ✅)

| Definition | Location | Description |
|------------|----------|-------------|
| `EntityId` | `ddd_model.h:12-14` | Domain entity identity (64-char UUID-like) |
| `Entity` | `ddd_model.h:16-19` | Versioned entity base |
| `OrderEntity` | `ddd_model.h:27-33` | Core business entity (customer, amount, currency) |
| `OrderStatus` | `ddd_model.h:35-42` | Enum: DRAFT→PLACED→CONFIRMED→SHIPPED→DELIVERED→CANCELLED |
| `AddressValueObject` | `ddd_model.h:44-48` | Immutable address value type |
| `MoneyValueObject` | `ddd_model.h:51-54` | Immutable money value type |
| `OrderAggregate` | `ddd_model.h:69-80` | Aggregate root with line items, shipping, billing |
| `RepositoryVTable` | `ddd_model.h:94-100` | Repository interface (find_by_id, save, delete, find_all) |
| `RepositoryResult` | `ddd_model.h:89-92` | Repository operation result |
| `EventStoreEvent` | `cqrs_es.h:14-22` | Append-only event with sequence, type, payload |
| `Command` / `CommandBus` | `cqrs_es.h:37-57` | CQRS write model |
| `Query` / `QueryBus` | `cqrs_es.h:59-80` | CQRS read model |
| `Projection` / `ProjectionEngine` | `cqrs_es.h:82-95` | Event-driven read model update |
| `EventSourcedSnapshot` | `cqrs_es.h:173-178` | Snapshot optimization for event sourcing |
| `SagaStep` / `SagaInstance` | `saga_orch.h:23-51` | Saga transaction step and instance |
| `SagaOrchestrator` | `saga_orch.h:53-57` | Centralized saga coordinator |
| `ChoreographyEvent` / `ChoreographyBus` | `saga_orch.h:69-96` | Event-driven saga coordination |
| `WorkflowState` / `WorkflowTransition` | `workflow_engine.h:34-59` | State machine definitions |
| `WorkflowInstance` | `workflow_engine.h:69-87` | Running workflow instance |
| `BPMNNode` / `BPMNSequenceFlow` | `bpmn_model.h:28-66` | BPMN 2.0 process elements |
| `BPMNProcess` / `BPMNProcessInstance` | `bpmn_model.h:92-106` | Executable BPMN process |
| `CircuitBreaker` | `business_patterns.h:53-64` | Nygard's circuit breaker |
| `IdempotencyKeyStore` | `business_patterns.h:110-115` | Exactly-once request dedup |
| `TokenBucket` | `business_patterns.h:133-141` | Turner's token bucket rate limiter |
| `ExponentialBackoff` | `business_patterns.h:169-178` | Exponential backoff with full jitter |
| `Bulkhead` | `business_patterns.h:206-215` | Nygard's bulkhead pattern |
| `ResiliencePolicy` | `business_patterns.h:241-247` | Composite resilience (Netflix Hystrix pattern) |
| `HealthChecker` | `business_patterns.h:279-284` | Phi-accrual failure detector |

## L2: Core Concepts (Complete ✅)

| Concept | Implementation | Reference |
|---------|---------------|-----------|
| Domain-Driven Design | `ddd_model.c` — Aggregate, Entity, ValueObject | Evans (2003) |
| CQRS (Command Query Responsibility Segregation) | `cqrs_es.c` — CommandBus + QueryBus | Young (2010) |
| Event Sourcing | `cqrs_es.c` — EventStore + ProjectionEngine | Fowler (2005) |
| Saga Orchestration | `saga_orch.c` — Central coordinator | Garcia-Molina & Salem (1987) |
| Saga Choreography | `saga_orch.c` — Event-driven services | Richardson (2018) |
| Finite State Machine | `workflow_engine.c` — States + transitions + guards | Harel (1987) |
| BPMN 2.0 Token Execution | `bpmn_model.c` — Token-based process engine | OMG (2011) |
| Circuit Breaker | `business_patterns.c` — 3-state machine | Nygard (2007) |
| Idempotency | `business_patterns.c` — Check-then-act with TTL | Brandenburger et al. (2015) |
| Rate Limiting | `business_patterns.c` — Token bucket algorithm | Turner (1986) |
| Exponential Backoff | `business_patterns.c` — Full jitter retry | AWS Architecture Blog (2015) |
| Bulkhead | `business_patterns.c` — Semaphore concurrency limit | Nygard (2007) |
| Failure Detection | `business_patterns.c` — Phi-accrual detector | Hayashibara et al. (2004) |

## L3: Engineering Structures (Complete ✅)

| Structure | Location | Description |
|-----------|----------|-------------|
| Aggregate Root Pattern | `ddd_model.c:68-197` | Transactional consistency boundary |
| Repository Pattern (In-Memory) | `ddd_model.c:199-283` | VTable-based polymorphic repository |
| Command Bus + Handler Registry | `cqrs_es.c:100-148` | Dynamic handler dispatch |
| Query Bus + Handler Registry | `cqrs_es.c:151-188` | Read-side handler dispatch |
| Projection Engine | `cqrs_es.c:191-257` | Event replay and polling |
| Event Stream | `cqrs_es.c:66-98` | Versioned event sequence |
| Orchestrator Queue | `saga_orch.c:146-195` | Active saga management |
| Event Log (Choreography) | `saga_orch.c:198-261` | Publish-subscribe event bus |
| Fork/Join State Machine | `workflow_engine.c:105-141` | Parallel branch execution |
| Token-Based Process Engine | `bpmn_model.c:208-312` | Gateways + parallel token flow |
| Health Check Registry | `business_patterns.c:648-749` | Dynamic target monitoring |

## L4: Standards/Theorems (Complete ✅)

| Theorem/Standard | Verification | Location |
|-----------------|--------------|----------|
| **CAP Theorem** (Brewer, 2000) | CQRS separates read/write consistency; Event Sourcing chooses AP over CP | `cqrs_es.c` header docs |
| **Idempotency Theorem** | Check-then-act linearizability within single process | `business_patterns.c:160-244` |
| **Amdahl's Law** | Fork/Join parallelism speedup bounded by serial portion | `workflow_engine.c:105-141` |
| **Little's Law** (L = λW) | Bulkhead concurrency = arrival_rate × service_time | `business_patterns.c:591-638` |
| **FLP Impossibility** (Fischer, Lynch, Paterson) | Deterministic async consensus impossible; motivates saga compensation | `saga_orch.c:101-122` |
| **PACELC Theorem** | CQRS: if Partition, choose Availability; Else minimize Latency | README discussion |
| **Saga Atomicity** | Forward recovery via retry + backward recovery via compensation | `saga_orch.c:70-122` |

## L5: Algorithms/Methods (Complete ✅)

| Algorithm | Complexity | Location |
|-----------|-----------|----------|
| **Circuit Breaker State Machine** | O(1) check, O(1) record | `business_patterns.c:45-145` |
| **Token Bucket Rate Limiting** | O(1) per check (amortized) | `business_patterns.c:249-305` |
| **Exponential Backoff with Full Jitter** | O(1) per retry | `business_patterns.c:310-370` |
| **Phi-Accrual Failure Detection** | O(1) per node per check (EMA) | `business_patterns.c:648-749` |
| **Saga Compensation Rollback** | O(n) where n = completed steps | `saga_orch.c:101-122` |
| **Event-Sourced Rebuild** | O(e) where e = event count | `cqrs_es.c:354-362` |
| **Snapshot Load + Catch-up** | O(1) + O(Δe) where Δe = events since snapshot | `cqrs_es.c:374-386` |
| **Gateway Evaluation (Exclusive)** | O(f) where f = outgoing flows | `bpmn_model.c:404-422` |
| **BPMN Token Parallel Split** | O(t) where t = target nodes | `bpmn_model.c:396-402` |

## L6: Canonical Problems (Complete ✅)

| Problem | Example | Description |
|---------|---------|-------------|
| **Order Management (DDD)** | `examples/example_ddd.c` | Aggregate Root + Repository + Domain Service |
| **CQRS + Event Sourcing** | `examples/example_cqrs.c` | Command dispatch → Event store → Query + Projection |
| **Distributed Transaction (Saga)** | `examples/example_saga.c` | Orchestration + Choreography patterns |
| **State Machine Workflow** | `examples/demo_workflow.c` (5 demos) | FSM + Fork/Join + Guards + Timers + Approval |
| **BPMN Process Engine** | `examples/demo_bpmn.c` (5 demos) | Order Fulfillment + Payment + Expense Approval + Tokens |

## L7: Applications (Partial+ ✅ — 3 applications)

| Application | Location | Description |
|-------------|----------|-------------|
| **Approval Workflow** | `workflow_engine.c:286-342` | Draft → Pending → Approved/Rejected/Revision cycle |
| **Order Fulfillment Process** | `bpmn_model.c:464-518` | Validate → Check Inventory → Gateway → Ship/Backorder → Notify |
| **Payment Processing** | `bpmn_model.c:520-559` | Authorize → Capture → Gateway(Refund?) → Refund/End |
| **DDD+CQRS Demo** | `demos/mini-ddd-cqrs/README.md` | Integration demo documentation |
| **Saga+Workflow Demo** | `demos/mini-saga-workflow/README.md` | Integration demo documentation |

## L8: Advanced Topics (Partial+ ✅ — 3 topics)

| Topic | Implementation | Reference |
|-------|---------------|-----------|
| **Composite Resilience** | `business_patterns.c:643-687` | Netflix Hystrix / resilience4j pattern |
| **Phi-Accrual Failure Detector** | `business_patterns.c:648-749` | Hayashibara et al. (2004), Akka/Cassandra |
| **Event Upcasting / Schema Evolution** | README discussion | Version-specific event handlers |
| **Snapshot Optimization** | `cqrs_es.c:364-386` | Memento pattern for event sourcing |

## L9: Industry Frontiers (Partial — documented)

| Frontier | Status | Notes |
|----------|--------|-------|
| **Confidential Computing for Event Stores** | Documented | Enclave-based event processing (Intel SGX) |
| **AI-Driven Workflow Optimization** | Documented | ML-based gateway decision making |
| **Quantum-Resistant Event Hashing** | Documented | Post-quantum signatures for event chains |
| **Serverless Saga Patterns** | Documented | Step Functions / Durable Functions equivalents |
