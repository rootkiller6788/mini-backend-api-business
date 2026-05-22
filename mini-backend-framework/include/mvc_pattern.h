#ifndef MVC_PATTERN_H
#define MVC_PATTERN_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MVC_MAX_ROUTES          64
#define MVC_MAX_PARAMS          16
#define MVC_MAX_VIEW_SIZE       8192
#define MVC_MAX_TEMPLATE_SIZE   16384
#define MVC_MAX_VAR_NAME        64
#define MVC_MAX_VAR_VALUE       256
#define MVC_HTTP_METHOD_LEN     8
#define MVC_PATH_LEN            256
#define MVC_ACTION_NAME_LEN     64

typedef enum {
    MVC_GET,
    MVC_POST,
    MVC_PUT,
    MVC_DELETE,
    MVC_PATCH
} MVCHttpMethod;

typedef struct {
    char name[MVC_MAX_VAR_NAME];
    char value[MVC_MAX_VAR_VALUE];
} MVCParam;

typedef struct {
    char      name[MVC_MAX_VAR_NAME];
    char      value[MVC_MAX_VAR_VALUE];
    bool      required;
    int       min_length;
    int       max_length;
    char      pattern[128];
} MVCField;

typedef struct {
    char      name[MVC_MAX_VAR_NAME];
    MVCField  fields[MVC_MAX_PARAMS];
    int       field_count;
    bool      is_valid;
    char      errors[MVC_MAX_PARAMS][256];
    int       error_count;
} MVCModel;

typedef struct {
    char     *template_content;
    MVCParam  variables[MVC_MAX_PARAMS];
    int       var_count;
    char      rendered[MVC_MAX_VIEW_SIZE];
} MVCView;

typedef int (*MVCAction)(MVCModel *model, MVCView *view, void *context);

typedef struct {
    char          path[MVC_PATH_LEN];
    char          name[MVC_ACTION_NAME_LEN];
    MVCHttpMethod method;
    MVCAction     handler;
} MVCRoute;

typedef struct {
    MVCRoute routes[MVC_MAX_ROUTES];
    int      route_count;
    void    *service_container;
} MVCController;

void mvc_model_init(MVCModel *model, const char *name);
void mvc_model_add_field(MVCModel *model, const char *name, bool required,
                         int min_len, int max_len, const char *pattern);
int  mvc_model_validate(MVCModel *model);
void mvc_model_set(MVCModel *model, const char *field, const char *value);
const char *mvc_model_get(const MVCModel *model, const char *field);

void mvc_view_init(MVCView *view);
void mvc_view_set_template(MVCView *view, const char *content);
void mvc_view_assign(MVCView *view, const char *name, const char *value);
int  mvc_view_render(MVCView *view);
const char *mvc_view_output(const MVCView *view);

void mvc_controller_init(MVCController *controller);
int  mvc_register_route(MVCController *controller, MVCHttpMethod method,
                        const char *path, const char *name, MVCAction handler);
int  mvc_dispatch(MVCController *controller, MVCHttpMethod method,
                  const char *path, MVCModel *model, MVCView *view,
                  void *context);

const char *mvc_method_string(MVCHttpMethod method);

#endif
