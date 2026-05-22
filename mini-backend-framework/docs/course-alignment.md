# Course Alignment — 课程对照

> 本模块与以下软件工程、后端架构与设计模式课程内容相对应。

## CMU 17-313: Foundations of Software Engineering

| CMU 17-313 Topic | mini-backend-framework Module | Description |
|---|---|---|
| Design Patterns | `di_container.h/c`, `demos/mini-di-container/` | Factory, Singleton, Dependency Injection patterns in C |
| Software Architecture | `mvc_pattern.h/c` | Model-View-Controller architecture, routing, dispatch |
| Testing and Quality | `validator.h/c` | Input validation, error collection, testable code design |
| Data Persistence | `orm_core.h/c`, `demos/mini-orm-mapping/` | ActiveRecord pattern, query building, schema definition |

## CMU 17-514: Software Architecture

| CMU 17-514 Topic | mini-backend-framework Module | Description |
|---|---|---|
| Architectural Patterns | `mvc_pattern.h/c` | MVC as an architectural pattern, separation of concerns |
| Dependency Management | `di_container.h/c` | Inversion of Control, service lifetimes, dependency graphs |
| Quality Attributes | `validator.h/c`, `serializer.h/c` | Input validation, data serialization as cross-cutting concerns |
| Component Interfaces | All headers | Clean C interfaces with opaque types and clear contracts |

## Stanford CS142: Web Applications

| CS142 Topic | mini-backend-framework Module | Description |
|---|---|---|
| MVC Architecture | `mvc_pattern.h/c`, `examples/mvc_demo.c` | Controller routing, model binding, view rendering |
| Data Modeling | `orm_core.h/c`, `demos/mini-orm-mapping/` | Table mapping, query construction, CRUD operations |
| Input Handling | `validator.h/c` | Form validation, sanitization rules, error reporting |
| RESTful Services | `mvc_pattern.h/c` | HTTP method routing, path parameter extraction, JSON serialization |

## MIT 6.031: Software Construction

| 6.031 Topic | mini-backend-framework Module | Description |
|---|---|---|
| Abstraction and Specification | All headers | Abstract data types in C with clear pre/post conditions |
| Designing for Change | `di_container.h/c` | Loose coupling via dependency injection |
| Debugging and Testing | `validator.h/c` | Structured validation with error collection |
| Recursive Data Types | `serializer.h/c` | Nested object serialization, array handling |

## CMU 15-214: Principles of Software Construction

| 15-214 Topic | mini-backend-framework Module | Description |
|---|---|---|
| Design Patterns | `di_container.h/c`, `demos/mini-di-container/` | IoC container, constructor injection, scoping |
| Framework Design | All modules | Building reusable, composable framework components |
| API Design | All headers | Consistent naming, clean interfaces, error semantics |
| Data Representation | `serializer.h/c` | Struct ↔ JSON mapping, field-level configuration |

## Topic Cross-Reference

| Concept | CMU 17-313 | Stanford CS142 | MIT 6.031 | Module Implementation |
|---|---|---|---|---|
| Dependency Injection | Lecture 14 | N/A | Lecture 12 | `di_container.c` |
| Active Record Pattern | Lecture 19 | Lecture 8 | N/A | `orm_core.c` |
| MVC Architecture | Lecture 16 | Lecture 2-3 | Lecture 20 | `mvc_pattern.c` |
| Template Rendering | N/A | Lecture 4 | N/A | `mvc_pattern.c` view |
| Input Validation | Lecture 11 | Lecture 5-6 | Lecture 8 | `validator.c` |
| JSON Serialization | N/A | Lecture 7 | N/A | `serializer.c` |
| Query Building | Lecture 19 | Lecture 8 | N/A | `orm_core.c` query builder |
| Service Scopes | Lecture 14 | N/A | Lecture 12 | `DIScope` enum |
| Route Dispatch | Lecture 16 | Lecture 3 | N/A | `mvc_dispatch()` |
| Field Mapping | N/A | N/A | N/A | `SerField` struct |

## Learning Path

Recommended study order for this module:

1. **DI Container** (`demos/mini-di-container/`) — Understand IoC, service lifetimes, and recursive resolution
2. **ORM Core** (`demos/mini-orm-mapping/`) — Learn table mapping, ActiveRecord, query building
3. **Validator** (`include/validator.h` + `examples/mvc_demo.c`) — Input validation rules and error collection
4. **Serializer** (`include/serializer.h`) — Struct-to-JSON and JSON-to-struct conversion
5. **MVC Pattern** (`examples/mvc_demo.c`) — Complete MVC flow: routing, model binding, view rendering

## Design Philosophy

This framework intentionally avoids:
- **Macro magic**: No code generation via X-macros or preprocessor tricks
- **Dynamic typing**: All type information is explicit in struct definitions
- **Hidden allocation**: Singleton cleanup is explicit via `di_destroy()`
- **Platform dependencies**: Pure C99 with libc only

The goal is educational clarity: every concept maps directly to readable C code that demonstrates the pattern without obscuring it behind abstraction layers.
