#ifndef DI_CONTAINER_H
#define DI_CONTAINER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define DI_MAX_SERVICES      64
#define DI_MAX_NAME          64
#define DI_MAX_TYPE          128
#define DI_MAX_DEPENDENCIES  16

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

void di_init(DIContainer *container);
int  di_register(DIContainer *container, const char *name, const char *type_name,
                 DIScope scope, DIFactory factory,
                 const char **deps, int dep_count);
void *di_resolve(DIContainer *container, const char *name);
void *di_resolve_type(DIContainer *container, const char *type_name);
void  di_begin_request(DIContainer *container);
void  di_end_request(DIContainer *container);
void  di_destroy(DIContainer *container);

#endif
