#ifndef ORM_CORE_H
#define ORM_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define ORM_MAX_COLUMNS     32
#define ORM_MAX_TABLE_NAME  64
#define ORM_MAX_COLUMN_NAME 64
#define ORM_MAX_QUERY_LEN   4096
#define ORM_MAX_JOINS       4
#define ORM_MAX_ROWS        1024

typedef enum {
    ORM_TYPE_INT,
    ORM_TYPE_INT64,
    ORM_TYPE_FLOAT,
    ORM_TYPE_DOUBLE,
    ORM_TYPE_STRING,
    ORM_TYPE_BOOL,
    ORM_TYPE_TEXT
} ORMColumnType;

typedef enum {
    ORM_OP_EQ,
    ORM_OP_NE,
    ORM_OP_LT,
    ORM_OP_LE,
    ORM_OP_GT,
    ORM_OP_GE,
    ORM_OP_LIKE,
    ORM_OP_IN,
    ORM_OP_IS_NULL
} ORMOp;

typedef enum {
    ORM_ORDER_ASC,
    ORM_ORDER_DESC
} ORMOrderDir;

typedef enum {
    ORM_JOIN_INNER,
    ORM_JOIN_LEFT,
    ORM_JOIN_RIGHT
} ORMJoinType;

typedef struct {
    char          name[ORM_MAX_COLUMN_NAME];
    ORMColumnType type;
    int           length;
    bool          primary_key;
    bool          auto_increment;
    bool          nullable;
    char          default_value[256];
} ORMColumnDef;

typedef struct {
    char          table_name[ORM_MAX_TABLE_NAME];
    ORMColumnDef  columns[ORM_MAX_COLUMNS];
    int           column_count;
    void         *model_struct;
    size_t        struct_size;
    int           pk_index;
} ORMMeta;

typedef struct {
    char         column[ORM_MAX_COLUMN_NAME];
    ORMOp        op;
    char         value[256];
    bool         is_or;
} ORMCondition;

typedef struct {
    char          table_a[ORM_MAX_TABLE_NAME];
    char          table_b[ORM_MAX_TABLE_NAME];
    char          col_a[ORM_MAX_COLUMN_NAME];
    char          col_b[ORM_MAX_COLUMN_NAME];
    ORMJoinType   type;
} ORMJoin;

typedef struct {
    char          table[ORM_MAX_TABLE_NAME];
    bool          select_all;
    char          columns[ORM_MAX_COLUMNS][ORM_MAX_COLUMN_NAME];
    int           column_count;
    ORMCondition  conditions[ORM_MAX_COLUMNS];
    int           cond_count;
    ORMJoin       joins[ORM_MAX_JOINS];
    int           join_count;
    char          order_by[ORM_MAX_COLUMN_NAME];
    ORMOrderDir   order_dir;
    int           limit_val;
    int           offset_val;
} ORMQuery;

typedef void *(*ORMDeserializeRow)(const char *row_data, void *target);

typedef struct {
    ORMMeta meta;
    void   *data;
    bool    is_new;
} ORMModel;

int  orm_define(const char *table_name, const ORMColumnDef *columns,
                int col_count, size_t struct_size);
int  orm_save(ORMModel *model, void *instance);
int  orm_find(ORMModel *model, int id);
int  orm_find_by(ORMModel *model, const char *column, const char *value);
int  orm_delete(ORMModel *model, int id);
int  orm_delete_by(ORMModel *model, const char *column, const char *value);

void orm_query_init(ORMQuery *q, const char *table);
void orm_query_select(ORMQuery *q, const char **cols, int count);
void orm_query_where(ORMQuery *q, const char *col, ORMOp op, const char *val);
void orm_query_or_where(ORMQuery *q, const char *col, ORMOp op, const char *val);
void orm_query_join(ORMQuery *q, ORMJoinType type, const char *ta,
                    const char *tb, const char *ca, const char *cb);
void orm_query_order(ORMQuery *q, const char *col, ORMOrderDir dir);
void orm_query_limit(ORMQuery *q, int limit, int offset);
int  orm_query_generate(const ORMQuery *q, char *out, int max_len);

void orm_init(void);
void orm_shutdown(void);

#endif
