#include "di_container.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void di_init(DIContainer *container) {
    memset(container, 0, sizeof(DIContainer));
}

static DIServiceEntry *di_find_entry(DIContainer *c, const char *name) {
    int i;
    for (i = 0; i < c->count; i++) {
        if (strcmp(c->entries[i].name, name) == 0) {
            return &c->entries[i];
        }
    }
    return NULL;
}

static DIServiceEntry *di_find_entry_by_type(DIContainer *c, const char *type_name) {
    int i;
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
    int i;
    DIServiceEntry *entry;

    if (container->count >= DI_MAX_SERVICES) return -1;
    if (dep_count > DI_MAX_DEPENDENCIES) return -2;

    entry = &container->entries[container->count];
    strncpy(entry->name, name, DI_MAX_NAME - 1);
    entry->name[DI_MAX_NAME - 1] = '\0';
    strncpy(entry->type_name, type_name, DI_MAX_TYPE - 1);
    entry->type_name[DI_MAX_TYPE - 1] = '\0';
    entry->scope       = scope;
    entry->factory     = factory;
    entry->instance    = NULL;
    entry->initialized = false;
    entry->dep_count   = dep_count;

    for (i = 0; i < dep_count; i++) {
        strncpy(entry->dependencies[i], deps[i], DI_MAX_NAME - 1);
        entry->dependencies[i][DI_MAX_NAME - 1] = '\0';
    }

    container->count++;
    return 0;
}

static void *di_build(DIContainer *container, DIServiceEntry *entry) {
    void *deps[DI_MAX_DEPENDENCIES];
    int i;

    for (i = 0; i < entry->dep_count; i++) {
        deps[i] = di_resolve(container, entry->dependencies[i]);
    }

    return entry->factory(container, deps, entry->dep_count);
}

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
            DIServiceEntry *e = di_find_entry(container, name);
            if (e) {
                int idx = (int)(e - container->entries);
                if (!container->request_instances[idx]) {
                    container->request_instances[idx] = di_build(container, e);
                }
                return container->request_instances[idx];
            }
        }
        return di_build(container, entry);

    default:
        return NULL;
    }
}

void *di_resolve_type(DIContainer *container, const char *type_name) {
    DIServiceEntry *entry = di_find_entry_by_type(container, type_name);
    if (!entry) return NULL;
    return di_resolve(container, entry->name);
}

void di_begin_request(DIContainer *container) {
    container->in_request = true;
    memset(container->request_instances, 0, sizeof(container->request_instances));
}

void di_end_request(DIContainer *container) {
    int i;
    for (i = 0; i < container->count; i++) {
        if (container->entries[i].scope == DI_SCOPE_REQUEST) {
            container->entries[i].initialized = false;
        }
        container->request_instances[i] = NULL;
    }
    container->in_request = false;
}

void di_destroy(DIContainer *container) {
    int i;
    for (i = 0; i < container->count; i++) {
        if (container->entries[i].instance) {
            free(container->entries[i].instance);
            container->entries[i].instance = NULL;
        }
    }
    container->count = 0;
}
