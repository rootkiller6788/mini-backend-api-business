/*
 * di_container.c — Dependency Injection Container Implementation
 *
 * L2: Inversion of Control — objects declare their dependencies;
 * the container resolves and injects them.
 *
 * L4: Dependency graph must be a DAG (no cycles). Cycle detection
 * uses DFS with three-color marking (WHITE/GRAY/BLACK).
 * Complexity: O(V+E) for cycle check, O(V*E) for full resolution.
 *
 * L3: Three lifecycle scopes — Singleton (one per container),
 * Transient (factory per resolve), Request (one per request).
 *
 * Reference: Martin Fowler, "IoC Container" (2004);
 * Prasanna, "Dependency Injection" (2009) Ch. 3-5.
 */

#include "di_container.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Color codes for DFS cycle detection --- */
#define DI_COLOR_WHITE 0   /* unvisited */
#define DI_COLOR_GRAY  1   /* in current DFS path (back-edge → cycle) */
#define DI_COLOR_BLACK 2   /* fully explored */

void di_init(DIContainer *container) {
    if (!container) return;
    memset(container, 0, sizeof(DIContainer));
}

/* O(n) linear search by service name */
static DIServiceEntry *di_find_entry(DIContainer *c, const char *name) {
    int i;
    if (!c || !name) return NULL;
    for (i = 0; i < c->count; i++) {
        if (strcmp(c->entries[i].name, name) == 0) {
            return &c->entries[i];
        }
    }
    return NULL;
}

/* O(n) linear search by type name */
static DIServiceEntry *di_find_entry_by_type(DIContainer *c, const char *type_name) {
    int i;
    if (!c || !type_name) return NULL;
    for (i = 0; i < c->count; i++) {
        if (strcmp(c->entries[i].type_name, type_name) == 0) {
            return &c->entries[i];
        }
    }
    return NULL;
}

int di_register(DIContainer *container, const char *name, const char *type_name,
                DIScope scope, DIFactory factory,
                const char **deps, int dep_count) {
    return di_register_dtor(container, name, type_name, scope, factory,
                            deps, dep_count, NULL);
}

int di_register_dtor(DIContainer *container, const char *name, const char *type_name,
                     DIScope scope, DIFactory factory,
                     const char **deps, int dep_count,
                     void (*dtor)(void *)) {
    int i;
    DIServiceEntry *entry;

    if (!container || !name || !type_name || !factory) return -3;
    if (container->count >= DI_MAX_SERVICES) return -1;
    if (dep_count < 0 || dep_count > DI_MAX_DEPENDENCIES) return -2;

    entry = &container->entries[container->count];
    strncpy(entry->name, name, DI_MAX_NAME - 1);
    entry->name[DI_MAX_NAME - 1] = '\0';
    strncpy(entry->type_name, type_name, DI_MAX_TYPE - 1);
    entry->type_name[DI_MAX_TYPE - 1] = '\0';
    entry->scope         = scope;
    entry->factory       = factory;
    entry->instance      = NULL;
    entry->initialized   = false;
    entry->dep_count     = dep_count;
    entry->tag_count     = 0;
    entry->destroy_func  = (void *)dtor;

    for (i = 0; i < dep_count; i++) {
        if (deps[i]) {
            strncpy(entry->dependencies[i], deps[i], DI_MAX_NAME - 1);
            entry->dependencies[i][DI_MAX_NAME - 1] = '\0';
        }
    }

    container->count++;
    return 0;
}

/*
 * L5: DFS-based cycle detection using 3-color marking.
 * Algorithm: DFS from each unvisited node. A back-edge
 * (GRAY→GRAY) indicates a cycle.
 * Reference: Cormen et al. §22.3 — DFS, §22.4 — Topological Sort.
 */
static int di_cycle_dfs(DIContainer *c, int *colors, int idx) {
    int i, dep_idx;
    colors[idx] = DI_COLOR_GRAY;

    for (i = 0; i < c->entries[idx].dep_count; i++) {
        DIServiceEntry *dep = di_find_entry(c, c->entries[idx].dependencies[i]);
        if (!dep) continue;
        dep_idx = (int)(dep - c->entries);

        if (colors[dep_idx] == DI_COLOR_GRAY) return 1;   /* back-edge: cycle */
        if (colors[dep_idx] == DI_COLOR_WHITE) {
            if (di_cycle_dfs(c, colors, dep_idx)) return 1;
        }
    }

    colors[idx] = DI_COLOR_BLACK;
    return 0;
}

int di_has_cycle(DIContainer *container, const char *name) {
    int colors[DI_MAX_SERVICES];
    DIServiceEntry *entry;
    int idx;

    if (!container || !name) return 0;
    entry = di_find_entry(container, name);
    if (!entry) return 0;

    memset(colors, 0, sizeof(colors));
    idx = (int)(entry - container->entries);
    return di_cycle_dfs(container, colors, idx);
}

int di_add_tag(DIContainer *container, const char *name, const char *tag) {
    DIServiceEntry *entry;

    if (!container || !name || !tag) return -1;
    entry = di_find_entry(container, name);
    if (!entry) return -1;
    if (entry->tag_count >= DI_MAX_TAGS) return -2;

    strncpy(entry->tags[entry->tag_count], tag, DI_MAX_NAME - 1);
    entry->tags[entry->tag_count][DI_MAX_NAME - 1] = '\0';
    entry->tag_count++;
    return 0;
}

int di_resolve_tagged(DIContainer *container, const char *tag,
                      void **results, int max_results) {
    int i, j, count = 0;

    if (!container || !tag || !results || max_results <= 0) return 0;

    for (i = 0; i < container->count && count < max_results; i++) {
        for (j = 0; j < container->entries[i].tag_count; j++) {
            if (strcmp(container->entries[i].tags[j], tag) == 0) {
                results[count++] = di_resolve(container, container->entries[i].name);
                break;
            }
        }
    }
    return count;
}

/*
 * L5: Topological sort via Kahn's algorithm for bulk resolution.
 * Resolves all singletons in dependency order, validating the
 * entire dependency graph at initialization time.
 * Complexity: O(V+E) time, O(V) space.
 */
int di_resolve_all(DIContainer *container) {
    int i, j, resolved = 0;
    int in_degree[DI_MAX_SERVICES];
    int queue[DI_MAX_SERVICES];
    int q_head = 0, q_tail = 0;

    if (!container) return -1;

    /* Compute in-degrees */
    for (i = 0; i < container->count; i++) {
        in_degree[i] = container->entries[i].dep_count;
        if (in_degree[i] == 0) {
            queue[q_tail++] = i;
        }
    }

    /* Kahn's algorithm */
    while (q_head < q_tail) {
        int u = queue[q_head++];
        resolved++;

        /* Resolve this singleton */
        if (container->entries[u].scope == DI_SCOPE_SINGLETON) {
            di_resolve(container, container->entries[u].name);
        }

        /* Decrease in-degree of dependents */
        for (i = 0; i < container->count; i++) {
            for (j = 0; j < container->entries[i].dep_count; j++) {
                if (strcmp(container->entries[i].dependencies[j],
                          container->entries[u].name) == 0) {
                    in_degree[i]--;
                    if (in_degree[i] == 0) {
                        queue[q_tail++] = i;
                    }
                }
            }
        }
    }

    /* If not all resolved, there's a cycle */
    return (resolved == container->count) ? 0 : -1;
}

/* Recursive dependency builder — resolves sub-dependencies first */
static void *di_build(DIContainer *container, DIServiceEntry *entry) {
    void *deps[DI_MAX_DEPENDENCIES];
    int i;

    /* Cyclic dependency guard: depth limit */
    if (container->resolve_depth > DI_MAX_SERVICES) {
        return NULL;
    }
    container->resolve_depth++;

    for (i = 0; i < entry->dep_count; i++) {
        deps[i] = di_resolve(container, entry->dependencies[i]);
        if (!deps[i] && entry->dependencies[i][0] != '\0') {
            container->resolve_depth--;
            return NULL;
        }
    }

    container->resolve_depth--;
    return entry->factory(container, deps, entry->dep_count);
}

void *di_resolve(DIContainer *container, const char *name) {
    DIServiceEntry *entry;

    if (!container || !name) return NULL;
    entry = di_find_entry(container, name);
    if (!entry) return NULL;

    switch (entry->scope) {
    case DI_SCOPE_SINGLETON:
        if (!entry->initialized) {
            container->resolve_depth = 0;
            entry->instance = di_build(container, entry);
            entry->initialized = (entry->instance != NULL);
        }
        return entry->instance;

    case DI_SCOPE_TRANSIENT:
        container->resolve_depth = 0;
        return di_build(container, entry);

    case DI_SCOPE_REQUEST:
        if (container->in_request) {
            int idx = (int)(entry - container->entries);
            if (!container->request_instances[idx]) {
                container->resolve_depth = 0;
                container->request_instances[idx] = di_build(container, entry);
            }
            return container->request_instances[idx];
        }
        container->resolve_depth = 0;
        return di_build(container, entry);

    default:
        return NULL;
    }
}

void *di_resolve_type(DIContainer *container, const char *type_name) {
    DIServiceEntry *entry;

    if (!container || !type_name) return NULL;
    entry = di_find_entry_by_type(container, type_name);
    if (!entry) return NULL;
    return di_resolve(container, entry->name);
}

void di_begin_request(DIContainer *container) {
    if (!container) return;
    container->in_request = true;
    memset(container->request_instances, 0, sizeof(container->request_instances));
}

void di_end_request(DIContainer *container) {
    int i;
    if (!container) return;

    for (i = 0; i < container->count; i++) {
        if (container->entries[i].scope == DI_SCOPE_REQUEST) {
            container->entries[i].initialized = false;
            container->entries[i].instance = NULL;
        }
        container->request_instances[i] = NULL;
    }
    container->in_request = false;
}

void di_destroy(DIContainer *container) {
    int i;
    if (!container) return;

    for (i = 0; i < container->count; i++) {
        DIServiceEntry *e = &container->entries[i];
        if (e->instance) {
            /* Call destructor if registered */
            if (e->destroy_func) {
                void (*dtor)(void *) = (void (*)(void *))e->destroy_func;
                dtor(e->instance);
            }
            free(e->instance);
            e->instance = NULL;
            e->initialized = false;
        }
    }
    container->count = 0;
}
