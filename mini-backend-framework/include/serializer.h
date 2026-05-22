#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define SER_MAX_FIELDS       64
#define SER_MAX_NAME         64
#define SER_MAX_JSON_SIZE    16384
#define SER_MAX_NESTED       8

typedef enum {
    SER_TYPE_INT,
    SER_TYPE_INT64,
    SER_TYPE_FLOAT,
    SER_TYPE_DOUBLE,
    SER_TYPE_STRING,
    SER_TYPE_BOOL,
    SER_TYPE_OBJECT,
    SER_TYPE_ARRAY,
    SER_TYPE_CUSTOM
} SerFieldType;

typedef int  (*SerEncoder)(const void *ptr, char *out, int max_len);
typedef int  (*SerDecoder)(void *ptr, const char *json);

typedef struct {
    char         name[SER_MAX_NAME];
    char         json_name[SER_MAX_NAME];
    SerFieldType type;
    size_t       offset;
    int          array_len;
    SerEncoder   encoder;
    SerDecoder   decoder;
    bool         ignore;
    bool         is_array;
    bool         is_nested;
} SerField;

typedef struct {
    SerField  fields[SER_MAX_FIELDS];
    int       field_count;
    size_t    struct_size;
    char      root_name[SER_MAX_NAME];
} Serializer;

void ser_init(Serializer *s, size_t struct_size, const char *root_name);
void ser_add_int(Serializer *s, const char *name, size_t offset,
                 const char *json_name);
void ser_add_int64(Serializer *s, const char *name, size_t offset,
                   const char *json_name);
void ser_add_float(Serializer *s, const char *name, size_t offset,
                   const char *json_name);
void ser_add_double(Serializer *s, const char *name, size_t offset,
                    const char *json_name);
void ser_add_string(Serializer *s, const char *name, size_t offset,
                    const char *json_name);
void ser_add_bool(Serializer *s, const char *name, size_t offset,
                  const char *json_name);
void ser_add_object(Serializer *s, const char *name, size_t offset,
                    const char *json_name, const Serializer *nested);
void ser_add_array(Serializer *s, const char *name, size_t offset,
                   const char *json_name, int array_len, SerFieldType elem_type);
void ser_add_custom(Serializer *s, const char *name, size_t offset,
                    const char *json_name, SerEncoder enc, SerDecoder dec);
void ser_ignore(Serializer *s, const char *name);

int  ser_to_json(const Serializer *s, const void *obj, char *out, int max_len);
int  ser_from_json(const Serializer *s, void *obj, const char *json);

void ser_strip_whitespace(char *s);
int  ser_json_get_string(const char *json, const char *key, char *out, int max);
int  ser_json_get_int(const char *json, const char *key, int default_val);
int  ser_json_get_bool(const char *json, const char *key);

#endif
