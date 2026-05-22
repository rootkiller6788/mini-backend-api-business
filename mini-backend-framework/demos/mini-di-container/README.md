# DI Container — Dependency Injection Container

## Overview

The Dependency Injection (DI) Container is the foundational component of the mini-backend-framework. It implements the Inversion of Control (IoC) pattern in pure C99, enabling loosely coupled, testable application architectures without requiring a higher-level language's reflection capabilities.

This container supports constructor injection with recursive dependency resolution, three service lifetimes (Singleton, Transient, Request), and both name-based and type-based service resolution. It demonstrates how modern backend patterns can be implemented in C with careful memory management and pointer discipline.

## Theory

### Inversion of Control and Dependency Injection

In traditional control flow, a module directly instantiates its dependencies:

```
UserController → new UserService() → new UserRepository() → new Database()
```

This creates tight coupling: the UserController must know how to construct a UserService, which must know how to construct a UserRepository, etc. Testing becomes difficult because dependencies cannot be easily substituted.

**Dependency Injection** inverts this: a container constructs and wires dependencies, then injects them into the client:

```
Container:
  1. Construct Database
  2. Construct UserRepository(database)
  3. Construct UserService(repository)
  4. Construct UserController(service)
  5. Return UserController
```

### Benefits of DI

1. **Loose Coupling**: Classes depend on interfaces, not concrete implementations
2. **Testability**: Dependencies can be replaced with mocks/stubs
3. **Configuration Centralization**: All wiring logic in one place
4. **Lifecycle Management**: The container controls when services are created and destroyed

### Service Lifetimes (Scopes)

#### Singleton

```
register("db", SINGLETON, db_factory)

Request 1: di_resolve("db") → create instance, cache it, return
Request 2: di_resolve("db") → return cached instance
Request 3: di_resolve("db") → return cached instance
```

One instance per container. Used for stateless services, database connections, caches.

**Memory**: The instance is created on first resolution and freed when `di_destroy()` is called.

#### Transient

```
register("logger", TRANSIENT, logger_factory)

Request 1: di_resolve("logger") → create new instance, return
Request 2: di_resolve("logger") → create new instance, return
```

New instance per resolution. Used for short-lived, stateful objects.

**Memory**: The caller is responsible for freeing transient instances.

#### Request Scope

```
register("session", REQUEST, session_factory)

di_begin_request()
  Request 1: di_resolve("session") → create instance, cache for this request
  Request 1: di_resolve("session") → return same instance
di_end_request() → clear request cache

di_begin_request()
  Request 2: di_resolve("session") → create new instance for new request
di_end_request()
```

One instance per request/context. Used for per-request objects like HTTP request context, user session data, database transactions.

### Recursive Dependency Resolution

The container builds a dependency graph and resolves it depth-first:

```
UserController
  └── UserService        (depends on)
        └── UserRepository  (depends on)
              └── Database     (depends on nothing)
```

When `di_resolve("user_ctrl")` is called:

1. Look up `UserController` service entry
2. Find its dependencies: `["UserService"]`
3. Recursively resolve `UserService`
4. Find `UserService`'s dependencies: `["UserRepository"]`
5. Recursively resolve `UserRepository`
6. Find `UserRepository`'s dependencies: `["Database"]`
7. Recursively resolve `Database`
8. `Database` has no dependencies → call `db_factory()`
9. Return `Database` to `UserRepository` factory
10. Call `repo_factory(Database)`, return to `UserService` factory
11. Call `svc_factory(UserRepository)`, return to `UserController` factory
12. Call `ctrl_factory(UserService)`, return to caller

### Service Registration

```
register(name, type, scope, factory, dependencies[], dep_count)

┌─────────────────────────────────────────────────┐
│ DIServiceEntry                                  │
│   .name        = "user_repo"                    │
│   .type_name   = "UserRepository"               │
│   .scope       = DI_SCOPE_SINGLETON             │
│   .factory     = repo_factory                   │
│   .dependencies[] = {"database"}                │
│   .dep_count   = 1                              │
│   .instance    = NULL (lazy init)               │
│   .initialized = false                          │
└─────────────────────────────────────────────────┘
```

### Type-Based Resolution

In addition to resolving by name (`di_resolve(container, "user_repo")`), the container supports type-based resolution (`di_resolve_type(container, "UserRepository")`). This enables:

- **Interface-based injection**: Register an interface type name, resolve by it
- **Auto-wiring**: When combined with conventions, services can be discovered by type
- **Plugin architectures**: Services can register as implementations of a known type

## Implementation

### Header: `include/di_container.h`

```c
typedef enum {
    DI_SCOPE_SINGLETON,
    DI_SCOPE_TRANSIENT,
    DI_SCOPE_REQUEST
} DIScope;

typedef void *(*DIFactory)(void *container, void **deps, int dep_count);

typedef struct {
    char       name[DI_MAX_NAME];
    char       type_name[DI_MAX_TYPE];
    DIScope    scope;
    DIFactory  factory;
    void      *instance;
    bool       initialized;
    char       dependencies[DI_MAX_DEPENDENCIES][DI_MAX_NAME];
    int        dep_count;
} DIServiceEntry;

typedef struct {
    DIServiceEntry entries[DI_MAX_SERVICES];
    int            count;
    bool           in_request;
    void          *request_instances[DI_MAX_SERVICES];
} DIContainer;
```

### Source: `src/di_container.c`

Key functions:

- **`di_init()`**: Zero-initializes the container
- **`di_register()`**: Adds a service entry with name, type, scope, factory, and dependency list
- **`di_resolve()`**: Resolves a service by name, handling scope logic and lazy initialization
- **`di_resolve_type()`**: Resolves a service by its type name
- **`di_build()`** (internal): Constructs a service by recursively resolving dependencies, then calling the factory with resolved deps
- **`di_begin_request()`/`di_end_request()`**: Manage request-scoped instance caching
- **`di_destroy()`**: Frees all singleton instances

### Dependency Resolution Flow

```c
void *di_resolve(DIContainer *container, const char *name) {
    DIServiceEntry *entry = di_find_entry(container, name);
    if (!entry) return NULL;

    switch (entry->scope) {
    case DI_SCOPE_SINGLETON:
        if (!entry->initialized) {
            entry->instance    = di_build(container, entry);
            entry->initialized = true;
        }
        return entry->instance;

    case DI_SCOPE_TRANSIENT:
        return di_build(container, entry);

    case DI_SCOPE_REQUEST:
        if (container->in_request) {
            int idx = (int)(entry - container->entries);
            if (!container->request_instances[idx]) {
                container->request_instances[idx] = di_build(container, entry);
            }
            return container->request_instances[idx];
        }
        return di_build(container, entry);
    }
}
```

### Circular Dependency Detection

While this implementation does not include explicit circular dependency detection, production systems should track a "resolving" stack. If a service is encountered while already being resolved, a circular dependency exists:

```
A → B → C → A  // circular!
```

## Demo: `examples/di_demo.c`

### Expected Output

```
=== Dependency Injection Container Demo ===

[1] Container initialized

[2] Registering services:
    Registered 'database' as Singleton
    Registered 'user_repo' as Transient
    Registered 'user_svc' as Singleton
    Registered 'user_ctrl' as Transient

[3] Resolving dependencies recursively:
  [Factory] Database created
  [Factory] UserRepository created (dep: Database)
  [Factory] UserService created (dep: UserRepository)
  [Factory] UserController created (dep: UserService)
    Resolved UserController -> 0x...
      -> UserService: 0x...
      -> UserRepository: 0x...
      -> Database: 0x... (conn: postgresql://localhost:5432/mydb)

[4] Singleton behavior:
    First  resolve: 0x...
    Second resolve: 0x...
    Same instance:  YES (Singleton)

[5] Transient behavior:
    First  resolve: 0x...
    Second resolve: 0x...
    Same instance:  NO (Transient)

[6] Type-based resolution:
    Resolved by type 'Database': 0x... (conn: postgresql://localhost:5432/mydb)

[7] Request scope demonstration:
    Request 1 UserController: 0x...
    Request 2 UserController: 0x...

[8] Cleanup:
    All singleton instances freed

=== DI Container Demo Complete ===
```

## Building

```bash
make di_demo
./bin/di_demo
```

## References

- Fowler, M. (2004). "Inversion of Control Containers and the Dependency Injection pattern"
- Seemann, M. (2011). "Dependency Injection in .NET" — Chapter 1-3: DI Fundamentals
- Prasanna, D. R. (2009). "Dependency Injection" — Chapter 2: DI Containers
- Spring Framework Documentation: "The IoC Container" (Conceptual Reference)
- Google Guice User's Guide: "Bindings and Scopes"
- Object Mentor: "The Dependency Inversion Principle" (Robert C. Martin)
