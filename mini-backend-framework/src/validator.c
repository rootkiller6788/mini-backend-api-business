#include "validator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void validator_init(Validator *v) {
    memset(v, 0, sizeof(Validator));
}

static void validator_add_rule(Validator *v, ValRuleType type,
                               const char *field, const char *param,
                               const char *message, ValCustomRule custom) {
    ValRule *rule;
    if (v->rule_count >= VAL_MAX_RULES) return;

    rule = &v->rules[v->rule_count];
    rule->type  = type;
    rule->custom = custom;
    strncpy(rule->field, field, VAL_MAX_FIELD_NAME - 1);
    rule->field[VAL_MAX_FIELD_NAME - 1] = '\0';
    if (param) {
        strncpy(rule->param, param, 127);
        rule->param[127] = '\0';
    } else {
        rule->param[0] = '\0';
    }
    if (message) {
        strncpy(rule->message, message, VAL_MAX_ERROR_MSG - 1);
        rule->message[VAL_MAX_ERROR_MSG - 1] = '\0';
    } else {
        rule->message[0] = '\0';
    }
    v->rule_count++;
}

void validator_rule_required(Validator *v, const char *field, const char *msg) {
    validator_add_rule(v, VAL_RULE_REQUIRED, field, NULL,
                       msg ? msg : "This field is required", NULL);
}

void validator_rule_min_length(Validator *v, const char *field, int min,
                               const char *msg) {
    char param[32];
    snprintf(param, sizeof(param), "%d", min);
    validator_add_rule(v, VAL_RULE_MIN_LENGTH, field, param,
                       msg ? msg : "Value too short", NULL);
}

void validator_rule_max_length(Validator *v, const char *field, int max,
                               const char *msg) {
    char param[32];
    snprintf(param, sizeof(param), "%d", max);
    validator_add_rule(v, VAL_RULE_MAX_LENGTH, field, param,
                       msg ? msg : "Value too long", NULL);
}

void validator_rule_regex(Validator *v, const char *field, const char *pattern,
                          const char *msg) {
    validator_add_rule(v, VAL_RULE_REGEX, field, pattern,
                       msg ? msg : "Value does not match pattern", NULL);
}

void validator_rule_email(Validator *v, const char *field, const char *msg) {
    validator_add_rule(v, VAL_RULE_EMAIL, field, NULL,
                       msg ? msg : "Invalid email format", NULL);
}

void validator_rule_numeric(Validator *v, const char *field, const char *msg) {
    validator_add_rule(v, VAL_RULE_NUMERIC, field, NULL,
                       msg ? msg : "Must be numeric", NULL);
}

void validator_rule_integer(Validator *v, const char *field, const char *msg) {
    validator_add_rule(v, VAL_RULE_INTEGER, field, NULL,
                       msg ? msg : "Must be an integer", NULL);
}

void validator_rule_min_value(Validator *v, const char *field, double min,
                              const char *msg) {
    char param[32];
    snprintf(param, sizeof(param), "%.6f", min);
    validator_add_rule(v, VAL_RULE_MIN_VALUE, field, param,
                       msg ? msg : "Value below minimum", NULL);
}

void validator_rule_max_value(Validator *v, const char *field, double max,
                              const char *msg) {
    char param[32];
    snprintf(param, sizeof(param), "%.6f", max);
    validator_add_rule(v, VAL_RULE_MAX_VALUE, field, param,
                       msg ? msg : "Value above maximum", NULL);
}

void validator_rule_custom(Validator *v, const char *field, ValCustomRule fn,
                           const char *param, const char *msg) {
    validator_add_rule(v, VAL_RULE_CUSTOM, field, param,
                       msg ? msg : "Custom validation failed", fn);
}

bool val_is_email(const char *value) {
    const char *at;
    int local_len, domain_len;
    const char *dot;

    if (!value || *value == '\0') return false;

    at = strchr(value, '@');
    if (!at) return false;

    local_len  = (int)(at - value);
    domain_len = (int)strlen(at + 1);

    if (local_len < 1 || local_len > 64) return false;
    if (domain_len < 3 || domain_len > 255) return false;

    dot = strchr(at + 1, '.');
    if (!dot) return false;
    if (dot == at + 1) return false;
    if (dot[1] == '\0') return false;

    if (strchr(value, ' ')) return false;
    if (strstr(value, "..")) return false;

    return true;
}

bool val_matches_regex(const char *value, const char *pattern) {
    if (!value || !pattern) return false;
    if (pattern[0] == '\0') return true;

    if (strcmp(pattern, "^[a-zA-Z0-9_]+$") == 0) {
        const char *p = value;
        if (*p == '\0') return false;
        while (*p) {
            if (!isalnum((unsigned char)*p) && *p != '_') return false;
            p++;
        }
        return true;
    }

    if (strcmp(pattern, "^[0-9]+$") == 0) {
        const char *p = value;
        if (*p == '\0') return false;
        while (*p) {
            if (!isdigit((unsigned char)*p)) return false;
            p++;
        }
        return true;
    }

    if (strcmp(pattern, "^[a-zA-Z]+$") == 0) {
        const char *p = value;
        if (*p == '\0') return false;
        while (*p) {
            if (!isalpha((unsigned char)*p)) return false;
            p++;
        }
        return true;
    }

    return true;
}

static int apply_rule(const ValRule *rule, const char *value, ValError *errors,
                      int max_errs, int *count) {
    char msg_buf[VAL_MAX_ERROR_MSG];
    int len;

    switch (rule->type) {
    case VAL_RULE_REQUIRED:
        if (!value || value[0] == '\0') {
            snprintf(msg_buf, sizeof(msg_buf), "%s: %s", rule->field, rule->message);
            if (errors && *count < max_errs) {
                strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
            }
            (*count)++;
            return 1;
        }
        break;

    case VAL_RULE_MIN_LENGTH:
        len = value ? (int)strlen(value) : 0;
        if (len < atoi(rule->param)) {
            snprintf(msg_buf, sizeof(msg_buf), "%s: %s (min %s)",
                     rule->field, rule->message, rule->param);
            if (errors && *count < max_errs) {
                strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
            }
            (*count)++;
            return 1;
        }
        break;

    case VAL_RULE_MAX_LENGTH:
        len = value ? (int)strlen(value) : 0;
        if (len > atoi(rule->param)) {
            snprintf(msg_buf, sizeof(msg_buf), "%s: %s (max %s)",
                     rule->field, rule->message, rule->param);
            if (errors && *count < max_errs) {
                strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
            }
            (*count)++;
            return 1;
        }
        break;

    case VAL_RULE_REGEX:
        if (!val_matches_regex(value, rule->param)) {
            snprintf(msg_buf, sizeof(msg_buf), "%s: %s (pattern: %s)",
                     rule->field, rule->message, rule->param);
            if (errors && *count < max_errs) {
                strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
            }
            (*count)++;
            return 1;
        }
        break;

    case VAL_RULE_EMAIL:
        if (!val_is_email(value)) {
            snprintf(msg_buf, sizeof(msg_buf), "%s: %s", rule->field, rule->message);
            if (errors && *count < max_errs) {
                strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
            }
            (*count)++;
            return 1;
        }
        break;

    case VAL_RULE_NUMERIC: {
        bool valid = value && value[0] != '\0';
        if (valid) {
            const char *p = value;
            if (*p == '-') p++;
            if (*p == '\0') valid = false;
            bool has_dot = false;
            while (*p) {
                if (*p == '.') {
                    if (has_dot) { valid = false; break; }
                    has_dot = true;
                } else if (!isdigit((unsigned char)*p)) {
                    valid = false;
                    break;
                }
                p++;
            }
        }
        if (!valid) {
            snprintf(msg_buf, sizeof(msg_buf), "%s: %s", rule->field, rule->message);
            if (errors && *count < max_errs) {
                strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
            }
            (*count)++;
            return 1;
        }
        break;
    }

    case VAL_RULE_INTEGER: {
        bool valid = value && value[0] != '\0';
        if (valid) {
            const char *p = value;
            if (*p == '-') p++;
            if (*p == '\0') valid = false;
            while (*p) {
                if (!isdigit((unsigned char)*p)) { valid = false; break; }
                p++;
            }
        }
        if (!valid) {
            snprintf(msg_buf, sizeof(msg_buf), "%s: %s", rule->field, rule->message);
            if (errors && *count < max_errs) {
                strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
            }
            (*count)++;
            return 1;
        }
        break;
    }

    case VAL_RULE_MIN_VALUE:
        if (value && value[0] != '\0') {
            double val_num = atof(value);
            double min_num = atof(rule->param);
            if (val_num < min_num) {
                snprintf(msg_buf, sizeof(msg_buf), "%s: %s (min %s)",
                         rule->field, rule->message, rule->param);
                if (errors && *count < max_errs) {
                    strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                    strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
                }
                (*count)++;
                return 1;
            }
        }
        break;

    case VAL_RULE_MAX_VALUE:
        if (value && value[0] != '\0') {
            double val_num = atof(value);
            double max_num = atof(rule->param);
            if (val_num > max_num) {
                snprintf(msg_buf, sizeof(msg_buf), "%s: %s (max %s)",
                         rule->field, rule->message, rule->param);
                if (errors && *count < max_errs) {
                    strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                    strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
                }
                (*count)++;
                return 1;
            }
        }
        break;

    case VAL_RULE_CUSTOM:
        if (rule->custom) {
            char custom_err[VAL_MAX_ERROR_MSG] = {0};
            if (!rule->custom(value, rule->param, custom_err)) {
                snprintf(msg_buf, sizeof(msg_buf), "%s: %s",
                         rule->field,
                         custom_err[0] ? custom_err : rule->message);
                if (errors && *count < max_errs) {
                    strncpy(errors[*count].field, rule->field, VAL_MAX_FIELD_NAME - 1);
                    strncpy(errors[*count].message, msg_buf, VAL_MAX_ERROR_MSG - 1);
                }
                (*count)++;
                return 1;
            }
        }
        break;
    }

    return 0;
}

int validator_validate_field(const Validator *v, const char *field,
                             const char *value, ValError *errors, int max_errs) {
    int i;
    int error_count = 0;

    for (i = 0; i < v->rule_count; i++) {
        if (strcmp(v->rules[i].field, field) == 0) {
            apply_rule(&v->rules[i], value, errors, max_errs, &error_count);
        }
    }
    return error_count;
}

int validator_validate(Validator *v, const char *field, const char *value) {
    int i;
    int error_count = 0;

    (void)field;
    for (i = 0; i < v->rule_count; i++) {
        if (field == NULL || strcmp(v->rules[i].field, field) == 0) {
            apply_rule(&v->rules[i], value, v->errors, VAL_MAX_ERRORS, &error_count);
        }
    }
    v->error_count = error_count;
    return error_count;
}

void validator_print_errors(const Validator *v) {
    int i;
    for (i = 0; i < v->error_count; i++) {
        printf("  [%s] %s\n", v->errors[i].field, v->errors[i].message);
    }
}
