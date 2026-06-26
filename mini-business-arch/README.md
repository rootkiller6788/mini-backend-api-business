# mini-business-arch — Business Architecture (C99)

A C99 library implementing core business architecture patterns: Domain-Driven Design,
CQRS with Event Sourcing, Saga orchestration/choreography, workflow engines, BPMN
process modeling, and enterprise resilience patterns.

## Module Status: COMPLETE ✅

| Level | Status | Detail |
|-------|--------|--------|
| **L1** Definitions | ✅ Complete | 28 struct/typedef/enum across 6 headers |
| **L2** Core Concepts | ✅ Complete | 13 concepts with dedicated implementations |
| **L3** Engineering Structures | ✅ Complete | 11 structures (repos, buses, engines) |
| **L4** Standards/Theorems | ✅ Complete | CAP, Idempotency, FLP, Amdahl, Little |
| **L5** Algorithms/Methods | ✅ Complete | 9 algorithms with complexity analysis |
| **L6** Canonical Problems | ✅ Complete | 5 runnable examples (DDD, CQRS, Saga, WF, BPMN) |
| **L7** Applications | ✅ Complete | 5 applications (3 implemented + 2 demo docs) |
| **L8** Advanced Topics | ✅ Complete | 4 topics (3 implemented: Hystrix, Phi-accrual, Snapshots) |
| **L9** Industry Frontiers | 🔶 Partial | 4 frontiers documented (SGX, AI workflows, PQ crypto, Serverless) |

**Lines of Code:** 3,980 (include/ + src/)  
**Tests:** 6 test suites, all passing (`make test`)  
**Examples:** 5 end-to-end runnable programs

## Modules

| Header | Pattern | Description |
|--------|---------|-------------|
| `ddd_model.h` | Domain-Driven Design | Entities, ValueObjects, Aggregates, Repositories, DomainServices |
| `cqrs_es.h` | CQRS + Event Sourcing | Command/Query buses, EventStore, Projections, Snapshots |
| `saga_orch.h` | Saga Pattern | Orchestration, Choreography, Compensating Transactions |
| `workflow_engine.h` | Workflow Engine | State machines, Fork/Join, Timers, Approval workflows |
| `bpmn_model.h` | BPMN 2.0 | Token-based process execution, Gateways, Tasks, Events |
| `business_patterns.h` | Resilience Patterns | Circuit Breaker, Bulkhead, Rate Limiter, Backoff, Health Check |

## Quick Start

```bash
make test        # Run all 6 test suites (assert-based)
make all         # Build all examples and demos
./bin/example_ddd
./bin/example_cqrs
./bin/example_saga
./bin/demo_workflow
./bin/demo_bpmn
```

## Core Definitions

### DDD (Domain-Driven Design)
- `EntityId`, `Entity` — Identity-based equality
- `AddressValueObject`, `MoneyValueObject` — Immutable value types
- `OrderAggregate` — Consistency boundary (root + line items)
- `Repository` (VTable) — Collection-like persistence abstraction
- `OrderDomainService` — Stateless domain operations

### CQRS + Event Sourcing
- `Command` / `CommandBus` — Write model (mutate state)
- `Query` / `QueryBus` — Read model (return DTOs)
- `EventStoreEvent` — Append-only event (sequence, type, payload)
- `ProjectionEngine` — Rebuild read models from events
- `EventSourcedSnapshot` — Performance optimization via snapshots

### Saga Pattern
- `SagaStep` — Forward action + compensating action
- `SagaInstance` — Running saga with step state tracking
- `SagaOrchestrator` — Central coordinator for orchestrated sagas
- `ChoreographyBus` — Event-driven saga coordination

### Workflow Engine
- `WorkflowState` — Named state with entry/exit actions
- `WorkflowTransition` — Event-triggered state change with guards
- `WorkflowInstance` — Running workflow with current state

### BPMN 2.0 Process Engine
- `BPMNNode` — Start/End events, User/Service/Script tasks, Gateways
- `BPMNSequenceFlow` — Conditional or default transitions
- `BPMNToken` — Token-based parallel execution

### Enterprise Resilience Patterns
- `CircuitBreaker` — 3-state machine (CLOSED → OPEN → HALF_OPEN)
- `IdempotencyKeyStore` — Exactly-once request deduplication
- `TokenBucket` — Rate limiting with configurable burst
- `ExponentialBackoff` — Full jitter retry strategy
- `Bulkhead` — Semaphore-based concurrency isolation
- `HealthChecker` — Phi-accrual failure detector

## Core Theorems

| Theorem | Application | Formula / Statement |
|---------|------------|---------------------|
| **CAP Theorem** (Brewer, 2000) | CQRS trades consistency for availability | Choose AP for EventStore, CP for CommandBus |
| **Idempotency** | Exactly-once request semantics | check_and_set(key) is linearizable in single process |
| **FLP Impossibility** (1985) | Motivates saga compensation | Deterministic async consensus impossible |
| **Amdahl's Law** | Fork/join speedup | S = 1 / ((1-P) + P/N) |
| **Little's Law** | Bulkhead sizing | L = λ × W (concurrency = arrival_rate × service_time) |

## Core Algorithms

| Algorithm | Complexity | Source |
|-----------|-----------|--------|
| Circuit Breaker State Machine | O(1) | `business_patterns.c` |
| Token Bucket Rate Limiter | O(1) amortized | `business_patterns.c` |
| Exponential Backoff with Full Jitter | O(1) per retry | `business_patterns.c` |
| Phi-Accrual Failure Detection | O(1) per node (EMA) | `business_patterns.c` |
| Saga Compensation Rollback | O(n) | `saga_orch.c` |
| Event-Sourced Rebuild | O(e) | `cqrs_es.c` |
| Snapshot + Catch-up | O(1) + O(Δe) | `cqrs_es.c` |
| BPMN Gateway Evaluation | O(f) | `bpmn_model.c` |
| Token Parallel Split | O(t) | `bpmn_model.c` |

## Canonical Problems

| Problem | Example | Pattern |
|---------|---------|---------|
| Order Management | `examples/example_ddd.c` | DDD: Aggregate + Repository + DomainService |
| CQRS + Event Sourcing | `examples/example_cqrs.c` | Command → EventStore → Projection → Query |
| Distributed Transaction | `examples/example_saga.c` | Saga: Orchestration + Choreography |
| State Machine Workflow | `examples/demo_workflow.c` (5 demos) | FSM, Fork/Join, Guards, Timers, Approval |
| BPMN Process | `examples/demo_bpmn.c` (5 demos) | Order Fulfillment, Payment, Expense Approval |

## Course Alignment (9 Schools)

| School | Key Courses | Implementation Mapping |
|--------|------------|----------------------|
| **MIT** | 6.824 Distributed Systems, 6.858 Security | Circuit Breaker, Idempotency, Event Sourcing |
| **Stanford** | CS 144 Networking, CS 244B Distributed, CS 245 DB | Token Bucket, Saga, CQRS+ES, Resilience |
| **Berkeley** | CS 162 OS, CS 186 DB, CS 294 AI | Bulkhead, Saga Txn, ML Gateways (L9) |
| **CMU** | 15-410 OS, 15-440 Distributed, 15-721 Advanced DB | Phi-Accrual, Consensus, Snapshots |
| **UT Austin** | CS 380D Distributed, CS 395T Systems ML | State Machine Replication, Backoff |
| **ETH Zurich** | 263-3501 Parallel, 263-0006 Architecture | Fork/Join, State Machines |
| **Cambridge** | Part II OS, Concurrent Systems, Compilers | Bulkhead, Saga Coordination, BPMN DSL |
| **清华** | 操作系统, 计算机网络, 编译原理 | Bulkhead, Token Bucket, State Machines |
| **Georgia Tech** | CS 6210 Adv OS, CS 6290 HPCA, CS 7641 ML | Saga, BPMN Tokens, ML Gateways (L9) |

## Structure

```
mini-business-arch/
├── include/                 # Headers (6 files, 1174 lines)
│   ├── ddd_model.h          # DDD type system
│   ├── cqrs_es.h            # CQRS + Event Sourcing
│   ├── saga_orch.h          # Saga orchestration/choreography
│   ├── workflow_engine.h    # Workflow state machine
│   ├── bpmn_model.h         # BPMN 2.0 process model
│   └── business_patterns.h  # Resilience patterns (NEW)
├── src/                     # Implementations (6 files, 2806 lines)
│   ├── ddd_model.c
│   ├── cqrs_es.c
│   ├── saga_orch.c
│   ├── workflow_engine.c
│   ├── bpmn_model.c
│   └── business_patterns.c  # NEW: Circuit Breaker, Bulkhead, etc.
├── tests/                   # Test suites (6 files, all passing)
│   ├── test_ddd.c
│   ├── test_cqrs.c
│   ├── test_saga.c
│   ├── test_workflow.c
│   ├── test_bpmn.c
│   └── test_patterns.c
├── examples/                # Runable examples (5 files)
│   ├── example_ddd.c
│   ├── example_cqrs.c
│   ├── example_saga.c
│   ├── demo_workflow.c
│   └── demo_bpmn.c
├── demos/                   # Integration demos
│   ├── mini-ddd-cqrs/
│   └── mini-saga-workflow/
├── docs/                    # Documentation
│   ├── ARCHITECTURE.md      # Architecture overview
│   ├── API.md               # Complete API reference
│   ├── knowledge-graph.md   # L1-L9 knowledge map
│   ├── coverage-report.md   # Coverage assessment
│   └── course-alignment.md  # 9-school course mapping
├── benches/                 # Performance benchmarks
├── Makefile                 # make test passes all tests
└── README.md                # This file
```

## Requirements

- C99 compiler (GCC, Clang, MSVC)
- Standard C library + math library (-lm)
- No external dependencies

## License

Internal use.
