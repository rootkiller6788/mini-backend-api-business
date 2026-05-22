#include "orm_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define ORM_MAX_META 32

static ORMMeta g_meta_registry[ORM_MAX_META];
static int     g_meta_count = 0;

static ORMMeta *orm_find_meta(const char *table_name) {
    int i;
    for (i = 0; i < g_meta_count; i++) {
        if (strcmp(g_meta_registry[i].table_name, table_name) == 0) {
            return &g_meta_registry[i];
        }
    }
    return NULL;
}

static const char *orm_column_type_string(ORMColumnType type) {
    switch (type) {
    case ORM_TYPE_INT:    return "INTEGER";
    case ORM_TYPE_INT64:  return "BIGINT";
    case ORM_TYPE_FLOAT:  return "REAL";
    case ORM_TYPE_DOUBLE: return "DOUBLE";
    case ORM_TYPE_STRING: return "VARCHAR";
    case ORM_TYPE_BOOL:   return "BOOLEAN";
    case ORM_TYPE_TEXT:   return "TEXT";
    default:              return "TEXT";
    }
}

static const char *orm_op_string(ORMOp op) {
    switch (op) {
    case ORM_OP_EQ:      return "=";
    case ORM_OP_NE:      return "!=";
    case ORM_OP_LT:      return "<";
    case ORM_OP_LE:      return "<=";
    case ORM_OP_GT:      return ">";
    case ORM_OP_GE:      return ">=";
    case ORM_OP_LIKE:    return "LIKE";
    case ORM_OP_IN:      return "IN";
    case ORM_OP_IS_NULL: return "IS NULL";
    default:             return "=";
    }
}

static const char *orm_join_string(ORMJoinType type) {
    switch (type) {
    case ORM_JOIN_INNER: return "INNER JOIN";
    case ORM_JOIN_LEFT:  return "LEFT JOIN";
    case ORM_JOIN_RIGHT: return "RIGHT JOIN";
    default:             return "INNER JOIN";
    }
}

void orm_init(void) {
    g_meta_count = 0;
    memset(g_meta_registry, 0, sizeof(g_meta_registry));
}

void orm_shutdown(void) {
    g_meta_count = 0;
}

int orm_define(const char *table_name, const ORMColumnDef *columns,
               int col_count, size_t struct_size) {
    ORMMeta *meta;
    int i;

    if (g_meta_count >= ORM_MAX_META) return -1;
    if (col_count > ORM_MAX_COLUMNS) return -2;

    meta = &g_meta_registry[g_meta_count];
    strncpy(meta->table_name, table_name, ORM_MAX_TABLE_NAME - 1);
    meta->table_name[ORM_MAX_TABLE_NAME - 1] = '\0';
    meta->column_count = col_count;
    meta->struct_size  = struct_size;
    meta->pk_index     = -1;

    for (i = 0; i < col_count; i++) {
        meta->columns[i] = columns[i];
        if (columns[i].primary_key) {
            meta->pk_index = i;
        }
    }

    g_meta_count++;
    return 0;
}

int orm_save(ORMModel *model, void *instance) {
    ORMMeta *meta;
    (void)instance;

    if (!model) return -1;
    meta = orm_find_meta(model->meta.table_name);
    if (!meta) return -2;

    model->is_new = false;
    model->data   = instance;

    return 0;
}

int orm_find(ORMModel *model, int id) {
    (void)model;
    (void)id;
    return 0;
}

int orm_find_by(ORMModel *model, const char *column, const char *value) {
    (void)model;
    (void)column;
    (void)value;
    return 0;
}

int orm_delete(ORMModel *model, int id) {
    (void)model;
    (void)id;
    return 0;
}

int orm_delete_by(ORMModel *model, const char *column, const char *value) {
    (void)model;
    (void)column;
    (void)value;
    return 0;
}

void orm_query_init(ORMQuery *q, const char *table) {
    memset(q, 0, sizeof(ORMQuery));
    strncpy(q->table, table, ORM_MAX_TABLE_NAME - 1);
    q->table[ORM_MAX_TABLE_NAME - 1] = '\0';
    q->select_all = true;
    q->limit_val  = -1;
    q->offset_val = 0;
}

void orm_query_select(ORMQuery *q, const char **cols, int count) {
    int i;
    q->select_all  = false;
    q->column_count = count < ORM_MAX_COLUMNS ? count : ORM_MAX_COLUMNS;
    for (i = 0; i < q->column_count; i++) {
        strncpy(q->columns[i], cols[i], ORM_MAX_COLUMN_NAME - 1);
        q->columns[i][ORM_MAX_COLUMN_NAME - 1] = '\0';
    }
}

void orm_query_where(ORMQuery *q, const char *col, ORMOp op, const char *val) {
    if (q->cond_count >= ORM_MAX_COLUMNS) return;
    strncpy(q->conditions[q->cond_count].column, col, ORM_MAX_COLUMN_NAME - 1);
    q->conditions[q->cond_count].column[ORM_MAX_COLUMN_NAME - 1] = '\0';
    q->conditions[q->cond_count].op    = op;
    q->conditions[q->cond_count].is_or = false;
    strncpy(q->conditions[q->cond_count].value, val, 255);
    q->conditions[q->cond_count].value[255] = '\0';
    q->cond_count++;
}

void orm_query_or_where(ORMQuery *q, const char *col, ORMOp op, const char *val) {
    if (q->cond_count >= ORM_MAX_COLUMNS) return;
    strncpy(q->conditions[q->cond_count].column, col, ORM_MAX_COLUMN_NAME - 1);
    q->conditions[q->cond_count].column[ORM_MAX_COLUMN_NAME - 1] = '\0';
    q->conditions[q->cond_count].op    = op;
    q->conditions[q->cond_count].is_or = true;
    strncpy(q->conditions[q->cond_count].value, val, 255);
    q->conditions[q->cond_count].value[255] = '\0';
    q->cond_count++;
}

void orm_query_join(ORMQuery *q, ORMJoinType type, const char *ta,
                    const char *tb, const char *ca, const char *cb) {
    if (q->join_count >= ORM_MAX_JOINS) return;
    strncpy(q->joins[q->join_count].table_a, ta, ORM_MAX_TABLE_NAME - 1);
    strncpy(q->joins[q->join_count].table_b, tb, ORM_MAX_TABLE_NAME - 1);
    strncpy(q->joins[q->join_count].col_a, ca, ORM_MAX_COLUMN_NAME - 1);
    strncpy(q->joins[q->join_count].col_b, cb, ORM_MAX_COLUMN_NAME - 1);
    q->joins[q->join_count].type = type;
    q->join_count++;
}

void orm_query_order(ORMQuery *q, const char *col, ORMOrderDir dir) {
    strncpy(q->order_by, col, ORM_MAX_COLUMN_NAME - 1);
    q->order_by[ORM_MAX_COLUMN_NAME - 1] = '\0';
    q->order_dir = dir;
}

void orm_query_limit(ORMQuery *q, int limit, int offset) {
    q->limit_val  = limit;
    q->offset_val = offset;
}

int orm_query_generate(const ORMQuery *q, char *out, int max_len) {
    int pos = 0;
    int i;
    int written;

    written = snprintf(out + pos, max_len - pos, "SELECT ");
    if (written < 0 || written >= max_len - pos) return -1;
    pos += written;

    if (q->select_all) {
        written = snprintf(out + pos, max_len - pos, "* ");
    } else {
        for (i = 0; i < q->column_count; i++) {
            written = snprintf(out + pos, max_len - pos, "%s%s",
                               q->columns[i],
                               i < q->column_count - 1 ? ", " : " ");
        }
    }
    if (written < 0 || written >= max_len - pos) return -1;
    pos += written;

    written = snprintf(out + pos, max_len - pos, "FROM %s", q->table);
    if (written < 0 || written >= max_len - pos) return -1;
    pos += written;

    for (i = 0; i < q->join_count; i++) {
        written = snprintf(out + pos, max_len - pos, " %s %s ON %s.%s = %s.%s",
                           orm_join_string(q->joins[i].type),
                           q->joins[i].table_b,
                           q->joins[i].table_a, q->joins[i].col_a,
                           q->joins[i].table_b, q->joins[i].col_b);
        if (written < 0 || written >= max_len - pos) return -1;
        pos += written;
    }

    if (q->cond_count > 0) {
        written = snprintf(out + pos, max_len - pos, " WHERE ");
        if (written < 0 || written >= max_len - pos) return -1;
        pos += written;

        for (i = 0; i < q->cond_count; i++) {
            const char *connector = (i > 0) ? (q->conditions[i].is_or ? "OR " : "AND ") : "";
            if (q->conditions[i].op == ORM_OP_IS_NULL) {
                written = snprintf(out + pos, max_len - pos, "%s%s IS NULL ",
                                   connector, q->conditions[i].column);
            } else {
                written = snprintf(out + pos, max_len - pos, "%s%s %s '%s' ",
                                   connector, q->conditions[i].column,
                                   orm_op_string(q->conditions[i].op),
                                   q->conditions[i].value);
            }
            if (written < 0 || written >= max_len - pos) return -1;
            pos += written;
        }
    }

    if (q->order_by[0] != '\0') {
        written = snprintf(out + pos, max_len - pos, "ORDER BY %s %s ",
                           q->order_by,
                           q->order_dir == ORM_ORDER_ASC ? "ASC" : "DESC");
        if (written < 0 || written >= max_len - pos) return -1;
        pos += written;
    }

    if (q->limit_val >= 0) {
        written = snprintf(out + pos, max_len - pos, "LIMIT %d OFFSET %d",
                           q->limit_val, q->offset_val);
        if (written < 0 || written >= max_len - pos) return -1;
        pos += written;
    }

    return pos;
}
