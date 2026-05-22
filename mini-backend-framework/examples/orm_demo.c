#include "orm_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int    id;
    char   name[64];
    int    age;
    char   email[128];
    double salary;
    bool   active;
} User;

static ORMColumnDef user_columns[] = {
    {"id",     ORM_TYPE_INT,    0,  true,  true,  false, ""},
    {"name",   ORM_TYPE_STRING, 64, false, false, false, ""},
    {"age",    ORM_TYPE_INT,    0,  false, false, true,  "0"},
    {"email",  ORM_TYPE_STRING, 128,false, false, false, ""},
    {"salary", ORM_TYPE_DOUBLE, 0,  false, false, true,  "0.0"},
    {"active", ORM_TYPE_BOOL,   0,  false, false, true,  "true"},
};

int main(void) {
    ORMModel user_model;
    User user;
    ORMQuery query;
    char sql[ORM_MAX_QUERY_LEN];
    int len;

    printf("=== ORM Core Demo: ActiveRecord + Query Builder ===\n\n");

    orm_init();
    printf("[1] ORM initialized\n");

    orm_define("users", user_columns, 6, sizeof(User));
    printf("    Defined table 'users' with %d columns\n", 6);
    memcpy(&user_model.meta, &((ORMMeta){
        .table_name = "users",
        .column_count = 6,
        .struct_size = sizeof(User),
        .pk_index = 0
    }), sizeof(ORMMeta));

    printf("\n[2] ActiveRecord: Save\n");
    memset(&user, 0, sizeof(User));
    user.id     = 1;
    strcpy(user.name, "Alice");
    user.age    = 30;
    strcpy(user.email, "alice@example.com");
    user.salary = 75000.0;
    user.active = true;

    orm_save(&user_model, &user);
    printf("    Saved User id=%d name=%s age=%d salary=%.2f active=%s\n",
           user.id, user.name, user.age, user.salary,
           user.active ? "true" : "false");

    printf("\n[3] ActiveRecord: Find\n");
    orm_find(&user_model, 1);
    printf("    Found User by primary key id=1\n");

    printf("\n[4] ActiveRecord: Find By\n");
    orm_find_by(&user_model, "email", "alice@example.com");
    printf("    Found User by email='alice@example.com'\n");

    printf("\n[5] ActiveRecord: Delete\n");
    orm_delete(&user_model, 1);
    printf("    Deleted User with id=1\n");

    printf("\n[6] Query Builder: Basic SELECT\n");
    orm_query_init(&query, "users");
    len = orm_query_generate(&query, sql, ORM_MAX_QUERY_LEN);
    printf("    SQL: %s\n", sql);

    printf("\n[7] Query Builder: SELECT with columns\n");
    orm_query_init(&query, "users");
    {
        const char *cols[] = {"id", "name", "email"};
        orm_query_select(&query, cols, 3);
    }
    len = orm_query_generate(&query, sql, ORM_MAX_QUERY_LEN);
    printf("    SQL: %s\n", sql);

    printf("\n[8] Query Builder: WHERE conditions\n");
    orm_query_init(&query, "users");
    orm_query_where(&query, "age", ORM_OP_GT, "25");
    orm_query_where(&query, "active", ORM_OP_EQ, "true");
    len = orm_query_generate(&query, sql, ORM_MAX_QUERY_LEN);
    printf("    SQL: %s\n", sql);

    printf("\n[9] Query Builder: OR WHERE\n");
    orm_query_init(&query, "users");
    orm_query_where(&query, "name", ORM_OP_LIKE, "%Alice%");
    orm_query_or_where(&query, "email", ORM_OP_LIKE, "%@example.com");
    len = orm_query_generate(&query, sql, ORM_MAX_QUERY_LEN);
    printf("    SQL: %s\n", sql);

    printf("\n[10] Query Builder: JOIN\n");
    orm_query_init(&query, "users");
    orm_query_join(&query, ORM_JOIN_INNER, "users", "orders",
                   "id", "user_id");
    len = orm_query_generate(&query, sql, ORM_MAX_QUERY_LEN);
    printf("    SQL: %s\n", sql);

    printf("\n[11] Query Builder: ORDER BY + LIMIT\n");
    orm_query_init(&query, "users");
    orm_query_where(&query, "active", ORM_OP_EQ, "true");
    orm_query_order(&query, "salary", ORM_ORDER_DESC);
    orm_query_limit(&query, 10, 0);
    len = orm_query_generate(&query, sql, ORM_MAX_QUERY_LEN);
    printf("    SQL: %s\n", sql);

    printf("\n[12] Query Builder: IS NULL\n");
    orm_query_init(&query, "users");
    orm_query_where(&query, "email", ORM_OP_IS_NULL, "");
    len = orm_query_generate(&query, sql, ORM_MAX_QUERY_LEN);
    printf("    SQL: %s\n", sql);

    printf("\n[13] Query Builder: Complex query\n");
    orm_query_init(&query, "users");
    orm_query_where(&query, "age", ORM_OP_GE, "18");
    orm_query_where(&query, "age", ORM_OP_LE, "65");
    orm_query_join(&query, ORM_JOIN_LEFT, "users", "departments",
                   "dept_id", "id");
    orm_query_order(&query, "name", ORM_ORDER_ASC);
    orm_query_limit(&query, 50, 100);
    len = orm_query_generate(&query, sql, ORM_MAX_QUERY_LEN);
    printf("    SQL: %s\n", sql);

    printf("\n=== ORM Core Demo Complete ===\n");
    return 0;
}
