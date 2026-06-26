#include "orm_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define ORM_MAX_META 32
#define ORM_MAX_ROWS_PER_TABLE 1024
#define ORM_MAX_DATA_SIZE      4096

/* L3: In-memory row store — each table has a ring buffer of row data */
typedef struct {
    char table[ORM_MAX_TABLE_NAME];
    char data[ORM_MAX_ROWS_PER_TABLE][ORM_MAX_DATA_SIZE];
    int  row_count;
    int  pk_vals[ORM_MAX_ROWS_PER_TABLE]; /* primary key index */
    int  pk_counter;
} ORMTable;

static ORMMeta g_meta_registry[ORM_MAX_META];
static int     g_meta_count = 0;
static ORMTable g_tables[ORM_MAX_META];
static int      g_table_count = 0;

/* Find or create table storage */
static ORMTable *orm_table(const char *table_name) {
    int i;
    for (i = 0; i < g_table_count; i++) {
        if (strcmp(g_tables[i].table, table_name) == 0)
            return &g_tables[i];
    }
    if (g_table_count >= ORM_MAX_META) return NULL;
    ORMTable *t = &g_tables[g_table_count];
    strncpy(t->table, table_name, ORM_MAX_TABLE_NAME - 1);
    t->table[ORM_MAX_TABLE_NAME - 1] = '\0';
    t->row_count = 0;
    t->pk_counter = 0;
    g_table_count++;
    return t;
}

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

/*
 * L5: In-memory CRUD operations using memcpy-based row storage.
 * Complexity: O(n) for find/delete (linear scan), O(1) for save.
 * This is a simplified implementation demonstrating the Active Record
 * pattern without an actual SQL database backend.
 *
 * Reference: Fowler, "Patterns of Enterprise Application Architecture"
 * (2002) Ch. 10 — Active Record.
 */

int orm_save(ORMModel *model, void *instance) {
    ORMTable *table;
    int pk_val;

    if (!model || !instance) return -1;
    table = orm_table(model->meta.table_name);
    if (!table) return -2;
    if (table->row_count >= ORM_MAX_ROWS_PER_TABLE) return -3;

    /* Auto-increment primary key */
    pk_val = ++table->pk_counter;
    table->pk_vals[table->row_count] = pk_val;

    /* Write instance data */
    memcpy(table->data[table->row_count], instance, model->meta.struct_size);
    table->row_count++;

    model->is_new = false;
    model->data   = instance;

    return pk_val;
}

int orm_find(ORMModel *model, int id) {
    ORMTable *table;
    int i;

    if (!model) return -1;
    table = orm_table(model->meta.table_name);
    if (!table) return -2;

    for (i = 0; i < table->row_count; i++) {
        if (table->pk_vals[i] == id) {
            if (model->data) {
                memcpy(model->data, table->data[i], model->meta.struct_size);
            }
            return id;
        }
    }
    return -1;
}

int orm_find_by(ORMModel *model, const char *column, const char *value) {
    ORMTable *table;
    ORMMeta *meta;
    int i, col_idx = -1;

    if (!model || !column || !value) return -1;
    table = orm_table(model->meta.table_name);
    if (!table) return -2;

    /* Find column index */
    meta = orm_find_meta(model->meta.table_name);
    if (meta) {
        for (i = 0; i < meta->column_count; i++) {
            if (strcmp(meta->columns[i].name, column) == 0) {
                col_idx = i;
                break;
            }
        }
    }
    if (col_idx < 0) return -3;

    /* Linear scan for matching row */
    for (i = 0; i < table->row_count; i++) {
        const char *row = table->data[i];
        int offset = 0;

        /* Calculate field offset based on column order */
        /* Simplified: assumes columns are in struct order with standard sizes */
        int j;
        for (j = 0; j < col_idx; j++) {
            switch (meta->columns[j].type) {
            case ORM_TYPE_INT:   case ORM_TYPE_BOOL: offset += sizeof(int); break;
            case ORM_TYPE_INT64: offset += sizeof(int64_t); break;
            case ORM_TYPE_FLOAT: offset += sizeof(float); break;
            case ORM_TYPE_DOUBLE: offset += sizeof(double); break;
            case ORM_TYPE_STRING:
            case ORM_TYPE_TEXT:  offset += meta->columns[j].length; break;
            default: break;
            }
        }

        if (meta->columns[col_idx].type == ORM_TYPE_STRING ||
            meta->columns[col_idx].type == ORM_TYPE_TEXT) {
            if (strcmp(row + offset, value) == 0) {
                if (model->data) {
                    memcpy(model->data, row, model->meta.struct_size);
                }
                return table->pk_vals[i];
            }
        }
    }
    return -1;
}

int orm_delete(ORMModel *model, int id) {
    ORMTable *table;
    int i;

    if (!model) return -1;
    table = orm_table(model->meta.table_name);
    if (!table) return -2;

    for (i = 0; i < table->row_count; i++) {
        if (table->pk_vals[i] == id) {
            /* Shift remaining rows down */
            int remaining = table->row_count - i - 1;
            if (remaining > 0) {
                memmove(table->data[i], table->data[i + 1],
                        remaining * ORM_MAX_DATA_SIZE);
                memmove(&table->pk_vals[i], &table->pk_vals[i + 1],
                        remaining * sizeof(int));
            }
            table->row_count--;
            return 0;
        }
    }
    return -1;
}

int orm_delete_by(ORMModel *model, const char *column, const char *value) {
    ORMTable *table;
    ORMMeta *meta;
    int i, j, col_idx = -1;

    if (!model || !column || !value) return -1;
    table = orm_table(model->meta.table_name);
    if (!table) return -2;

    meta = orm_find_meta(model->meta.table_name);
    if (meta) {
        for (i = 0; i < meta->column_count; i++) {
            if (strcmp(meta->columns[i].name, column) == 0) { col_idx = i; break; }
        }
    }
    if (col_idx < 0) return -3;

    for (i = 0; i < table->row_count; i++) {
        const char *row = table->data[i];
        int offset = 0;
        for (j = 0; j < col_idx; j++) {
            switch (meta->columns[j].type) {
            case ORM_TYPE_INT: case ORM_TYPE_BOOL: offset += sizeof(int); break;
            case ORM_TYPE_INT64: offset += sizeof(int64_t); break;
            case ORM_TYPE_FLOAT: offset += sizeof(float); break;
            case ORM_TYPE_DOUBLE: offset += sizeof(double); break;
            case ORM_TYPE_STRING: case ORM_TYPE_TEXT: offset += meta->columns[j].length; break;
            default: break;
            }
        }

        if ((meta->columns[col_idx].type == ORM_TYPE_STRING ||
             meta->columns[col_idx].type == ORM_TYPE_TEXT) &&
            strcmp(row + offset, value) == 0) {
            int remaining = table->row_count - i - 1;
            if (remaining > 0) {
                memmove(table->data[i], table->data[i + 1], remaining * ORM_MAX_DATA_SIZE);
                memmove(&table->pk_vals[i], &table->pk_vals[i + 1], remaining * sizeof(int));
            }
            table->row_count--;
            return 0;
        }
    }
    return -1;
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
