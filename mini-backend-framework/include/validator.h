#ifndef VALIDATOR_H
#define VALIDATOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define VAL_MAX_RULES       32
#define VAL_MAX_ERRORS      16
#define VAL_MAX_RULE_NAME   64
#define VAL_MAX_ERROR_MSG   256
#define VAL_MAX_FIELD_NAME  64
#define VAL_MAX_VALUE_LEN   1024

typedef enum {
    VAL_RULE_REQUIRED,
    VAL_RULE_MIN_LENGTH,
    VAL_RULE_MAX_LENGTH,
    VAL_RULE_REGEX,
    VAL_RULE_EMAIL,
    VAL_RULE_NUMERIC,
    VAL_RULE_INTEGER,
    VAL_RULE_MIN_VALUE,
    VAL_RULE_MAX_VALUE,
    VAL_RULE_CUSTOM
} ValRuleType;

typedef bool (*ValCustomRule)(const char *value, const char *param, char *error_out);

typedef struct {
    char         field[VAL_MAX_FIELD_NAME];
    char         message[VAL_MAX_ERROR_MSG];
} ValError;

typedef struct {
    ValRuleType   type;
    char          field[VAL_MAX_FIELD_NAME];
    char          param[128];
    char          message[VAL_MAX_ERROR_MSG];
    ValCustomRule custom;
} ValRule;

typedef struct {
    ValRule   rules[VAL_MAX_RULES];
    int       rule_count;
    ValError  errors[VAL_MAX_ERRORS];
    int       error_count;
} Validator;

void validator_init(Validator *v);
void validator_rule_required(Validator *v, const char *field, const char *msg);
void validator_rule_min_length(Validator *v, const char *field, int min,
                               const char *msg);
void validator_rule_max_length(Validator *v, const char *field, int max,
                               const char *msg);
void validator_rule_regex(Validator *v, const char *field, const char *pattern,
                          const char *msg);
void validator_rule_email(Validator *v, const char *field, const char *msg);
void validator_rule_numeric(Validator *v, const char *field, const char *msg);
void validator_rule_integer(Validator *v, const char *field, const char *msg);
void validator_rule_min_value(Validator *v, const char *field, double min,
                              const char *msg);
void validator_rule_max_value(Validator *v, const char *field, double max,
                              const char *msg);
void validator_rule_custom(Validator *v, const char *field, ValCustomRule fn,
                           const char *param, const char *msg);

int  validator_validate_field(const Validator *v, const char *field,
                              const char *value, ValError *errors, int max_errs);
int  validator_validate(Validator *v, const char *field, const char *value);
void validator_print_errors(const Validator *v);

bool val_is_email(const char *value);
bool val_matches_regex(const char *value, const char *pattern);

#endif
