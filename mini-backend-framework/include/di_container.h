#ifndef DI_CONTAINER_H
#define DI_CONTAINER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define DI_MAX_SERVICES      64
#define DI_MAX_NAME          64
#define DI_MAX_TYPE          128
#define DI_MAX_DEPENDENCIES  16
#define DI_MAX_TAGS          8

/*
 * L1: Core Definitions — Dependency Injection Container
 *
 * Inversion of Control (IoC) principle: objects do not create their
 * own dependencies; they are provided ("injected") by a container.
 * Reference: Martin Fowler, "Inversion of Control Containers and the
 * Dependency Injection pattern" (2004).
 */

typedef enum {
    DI_SCOPE_SINGLETON,   /* One instance per container lifetime  */
    DI_SCOPE_TRANSIENT,   /* New instance on every resolve        */
    DI_SCOPE_REQUEST      /* One instance per request context     */
} DIScope;

/* Factory signature: receives the container (for recursive resolution),
 * pre-resolved dependency array, and dependency count. */
typedef void *(*DIFactory)(void *container, void **deps, int dep_count);

/* L4 Theorem: Dependency graph must be a DAG (Directed Acyclic Graph).
 * Cycle detection ensures no circular dependency deadlock.
 * This is equivalent to topological sort existence check — O(V+E). */

typedef struct {
    char       name[DI_MAX_NAME];                          /* unique service identifier */
    char       type_name[DI_MAX_TYPE];                     /* type for type-based lookup */
    DIScope    scope;                                      /* lifecycle scope */
    DIFactory  factory;                                    /* constructor function */
    void      *instance;                                   /* singleton/request cached instance */
    bool       initialized;                                /* lazy-init guard */
    char       dependencies[DI_MAX_DEPENDENCIES][DI_MAX_NAME]; /* named dependencies */
    int        dep_count;
    char       tags[DI_MAX_TAGS][DI_MAX_NAME];             /* metadata tags for filtering */
    int        tag_count;
    void      *destroy_func;                               /* optional destructor */
} DIServiceEntry;

typedef struct {
    DIServiceEntry entries[DI_MAX_SERVICES];
    int            count;
    bool           in_request;                             /* request scope active */
    void          *request_instances[DI_MAX_SERVICES];
    int            resolve_depth;                          /* cycle detection depth counter */
} DIContainer;

/* --- Public API --- */

/* Initialize container to empty state */
void di_init(DIContainer *container);

/* Register a service definition. deps may be NULL if dep_count==0.
 * Returns 0 on success, -1 on capacity exceeded, -2 on too many deps. */
int  di_register(DIContainer *container, const char *name, const char *type_name,
                 DIScope scope, DIFactory factory,
                 const char **deps, int dep_count);

/* Register with destructor callback (L2: resource lifecycle) */
int  di_register_dtor(DIContainer *container, const char *name, const char *type_name,
                      DIScope scope, DIFactory factory,
                      const char **deps, int dep_count,
                      void (*dtor)(void *));

/* L5: Topological-sort based bulk resolution. Resolves all singletons
 * in dependency order for initialization-time validation. */
int  di_resolve_all(DIContainer *container);

/* Resolve a service by name. Returns NULL if not found or cycle detected. */
void *di_resolve(DIContainer *container, const char *name);

/* Resolve a service by type name (first match) */
void *di_resolve_type(DIContainer *container, const char *type_name);

/* L3: Tag-based service query — resolve all services with given tag */
int  di_resolve_tagged(DIContainer *container, const char *tag,
                       void **results, int max_results);

/* Add a metadata tag to a registered service */
int  di_add_tag(DIContainer *container, const char *name, const char *tag);

/* Cycle detection: returns 1 if a dependency cycle exists starting from name */
int  di_has_cycle(DIContainer *container, const char *name);

/* Request scope lifecycle */
void di_begin_request(DIContainer *container);
void di_end_request(DIContainer *container);

/* Destroy all singleton instances and reset container */
void di_destroy(DIContainer *container);

#endif
