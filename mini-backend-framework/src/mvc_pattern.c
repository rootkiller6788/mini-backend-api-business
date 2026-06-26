#include "mvc_pattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *mvc_method_string(MVCHttpMethod method) {
    switch (method) {
    case MVC_GET:    return "GET";
    case MVC_POST:   return "POST";
    case MVC_PUT:    return "PUT";
    case MVC_DELETE: return "DELETE";
    case MVC_PATCH:  return "PATCH";
    default:         return "UNKNOWN";
    }
}

void mvc_model_init(MVCModel *model, const char *name) {
    memset(model, 0, sizeof(MVCModel));
    strncpy(model->name, name, MVC_MAX_VAR_NAME - 1);
    model->name[MVC_MAX_VAR_NAME - 1] = '\0';
    model->is_valid = true;
}

void mvc_model_add_field(MVCModel *model, const char *name, bool required,
                         int min_len, int max_len, const char *pattern) {
    MVCField *f;
    if (model->field_count >= MVC_MAX_PARAMS) return;

    f = &model->fields[model->field_count];
    strncpy(f->name, name, MVC_MAX_VAR_NAME - 1);
    f->name[MVC_MAX_VAR_NAME - 1] = '\0';
    f->value[0] = '\0';
    f->required   = required;
    f->min_length = min_len;
    f->max_length = max_len;
    if (pattern) {
        strncpy(f->pattern, pattern, 127);
        f->pattern[127] = '\0';
    } else {
        f->pattern[0] = '\0';
    }
    model->field_count++;
}

void mvc_model_set(MVCModel *model, const char *field, const char *value) {
    int i;
    for (i = 0; i < model->field_count; i++) {
        if (strcmp(model->fields[i].name, field) == 0) {
            strncpy(model->fields[i].value, value, MVC_MAX_VAR_VALUE - 1);
            model->fields[i].value[MVC_MAX_VAR_VALUE - 1] = '\0';
            return;
        }
    }
    if (model->field_count < MVC_MAX_PARAMS) {
        MVCField *f = &model->fields[model->field_count];
        strncpy(f->name, field, MVC_MAX_VAR_NAME - 1);
        f->name[MVC_MAX_VAR_NAME - 1] = '\0';
        strncpy(f->value, value, MVC_MAX_VAR_VALUE - 1);
        f->value[MVC_MAX_VAR_VALUE - 1] = '\0';
        f->required   = false;
        f->min_length = 0;
        f->max_length = 0;
        f->pattern[0] = '\0';
        model->field_count++;
    }
}

const char *mvc_model_get(const MVCModel *model, const char *field) {
    int i;
    for (i = 0; i < model->field_count; i++) {
        if (strcmp(model->fields[i].name, field) == 0) {
            return model->fields[i].value;
        }
    }
    return NULL;
}

static bool matches_pattern(const char *text, const char *pattern) {
    if (!pattern || pattern[0] == '\0') return true;

    if (strcmp(pattern, "alphanumeric") == 0) {
        while (*text) {
            if (!((*text >= 'a' && *text <= 'z') ||
                  (*text >= 'A' && *text <= 'Z') ||
                  (*text >= '0' && *text <= '9'))) {
                return false;
            }
            text++;
        }
        return true;
    }

    if (strcmp(pattern, "alpha") == 0) {
        while (*text) {
            if (!((*text >= 'a' && *text <= 'z') ||
                  (*text >= 'A' && *text <= 'Z'))) {
                return false;
            }
            text++;
        }
        return true;
    }

    if (strcmp(pattern, "numeric") == 0) {
        if (*text == '\0') return false;
        while (*text) {
            if (*text < '0' || *text > '9') return false;
            text++;
        }
        return true;
    }

    return true;
}

int mvc_model_validate(MVCModel *model) {
    int i;
    int len;

    model->is_valid    = true;
    model->error_count = 0;

    for (i = 0; i < model->field_count; i++) {
        MVCField *f = &model->fields[i];
        len = (int)strlen(f->value);

        if (f->required && len == 0) {
            snprintf(model->errors[model->error_count], 256,
                     "Field '%s' is required", f->name);
            model->error_count++;
            model->is_valid = false;
            continue;
        }

        if (len > 0) {
            if (f->min_length > 0 && len < f->min_length) {
                snprintf(model->errors[model->error_count], 256,
                         "Field '%s' min length is %d (got %d)",
                         f->name, f->min_length, len);
                model->error_count++;
                model->is_valid = false;
            }

            if (f->max_length > 0 && len > f->max_length) {
                snprintf(model->errors[model->error_count], 256,
                         "Field '%s' max length is %d (got %d)",
                         f->name, f->max_length, len);
                model->error_count++;
                model->is_valid = false;
            }

            if (f->pattern[0] != '\0' && !matches_pattern(f->value, f->pattern)) {
                snprintf(model->errors[model->error_count], 256,
                         "Field '%s' does not match pattern '%s'",
                         f->name, f->pattern);
                model->error_count++;
                model->is_valid = false;
            }

            if (model->error_count >= MVC_MAX_PARAMS) break;
        }
    }

    return model->is_valid ? 0 : -1;
}

void mvc_view_init(MVCView *view) {
    memset(view, 0, sizeof(MVCView));
}

void mvc_view_set_template(MVCView *view, const char *content) {
    view->template_content = (char *)content;
}

void mvc_view_assign(MVCView *view, const char *name, const char *value) {
    if (view->var_count >= MVC_MAX_PARAMS) return;
    strncpy(view->variables[view->var_count].name, name, MVC_MAX_VAR_NAME - 1);
    view->variables[view->var_count].name[MVC_MAX_VAR_NAME - 1] = '\0';
    strncpy(view->variables[view->var_count].value, value, MVC_MAX_VAR_VALUE - 1);
    view->variables[view->var_count].value[MVC_MAX_VAR_VALUE - 1] = '\0';
    view->var_count++;
}

int mvc_view_render(MVCView *view) {
    const char *src;
    char *dst;
    int i;
    int pos;

    if (!view->template_content) return -1;

    src = view->template_content;
    dst = view->rendered;
    pos = 0;

    while (*src && pos < MVC_MAX_VIEW_SIZE - 1) {
        if (*src == '{' && *(src + 1) == '{') {
            src += 2;
            while (*src == ' ') src++;

            char varname[MVC_MAX_VAR_NAME];
            int vn = 0;
            /* Read variable name until } or end-of-string */
            while (*src && *src != '}' && vn < MVC_MAX_VAR_NAME - 1) {
                varname[vn++] = *src++;
            }
            varname[vn] = '\0';

            /* Strip trailing spaces from variable name */
            while (vn > 0 && varname[vn - 1] == ' ') {
                vn--;
                varname[vn] = '\0';
            }

            while (*src == ' ' || *src == '}') src++;
            if (*src == '}') src++;

            const char *replacement = NULL;
            for (i = 0; i < view->var_count; i++) {
                if (strcmp(view->variables[i].name, varname) == 0) {
                    replacement = view->variables[i].value;
                    break;
                }
            }
            if (replacement) {
                int rlen = (int)strlen(replacement);
                if (pos + rlen < MVC_MAX_VIEW_SIZE - 1) {
                    strcpy(dst + pos, replacement);
                    pos += rlen;
                }
            }
        } else {
            dst[pos++] = *src++;
        }
    }
    dst[pos] = '\0';

    return pos;
}

const char *mvc_view_output(const MVCView *view) {
    return view->rendered;
}

void mvc_controller_init(MVCController *controller) {
    memset(controller, 0, sizeof(MVCController));
}

int mvc_register_route(MVCController *controller, MVCHttpMethod method,
                       const char *path, const char *name, MVCAction handler) {
    MVCRoute *route;
    if (controller->route_count >= MVC_MAX_ROUTES) return -1;

    route = &controller->routes[controller->route_count];
    route->method  = method;
    route->handler = handler;
    strncpy(route->path, path, MVC_PATH_LEN - 1);
    route->path[MVC_PATH_LEN - 1] = '\0';
    strncpy(route->name, name, MVC_ACTION_NAME_LEN - 1);
    route->name[MVC_ACTION_NAME_LEN - 1] = '\0';
    controller->route_count++;
    return 0;
}

static bool path_matches(const char *route_path, const char *request_path) {
    while (*route_path && *request_path) {
        if (*route_path == '{') {
            route_path++;
            while (*route_path && *route_path != '}') route_path++;
            if (*route_path == '}') route_path++;

            while (*request_path && *request_path != '/') request_path++;
            continue;
        }

        if (*route_path != *request_path) return false;
        route_path++;
        request_path++;
    }
    return *route_path == '\0' && *request_path == '\0';
}

int mvc_dispatch(MVCController *controller, MVCHttpMethod method,
                 const char *path, MVCModel *model, MVCView *view,
                 void *context) {
    int i;

    for (i = 0; i < controller->route_count; i++) {
        MVCRoute *route = &controller->routes[i];
        if (route->method == method && path_matches(route->path, path)) {
            return route->handler(model, view, context);
        }
    }
    return -1;
}
