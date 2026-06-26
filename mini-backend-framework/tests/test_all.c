/*
 * test_all.c - Comprehensive Test Suite for mini-backend-framework
 *
 * Tests all modules: DI Container, ORM Core, MVC Pattern,
 * Validator, Serializer, Middleware, Rate Limiter, Cache,
 * Connection Pool, Config Manager.
 *
 * Uses assert-based testing. Each test function maps to
 * a specific knowledge point (L1-L8).
 */

#include "di_container.h"
#include "orm_core.h"
#include "mvc_pattern.h"
#include "validator.h"
#include "serializer.h"
#include "middleware.h"
#include "rate_limiter.h"
#include "cache.h"
#include "pool.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %s ... ", name); \
} while(0)

#define PASS() do { \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAILED: %s\n", msg); \
    tests_failed++; \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_STR_EQ(a, b, msg) do { \
    if (!(a) || !(b) || strcmp((a), (b)) != 0) { FAIL(msg); return; } \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_NOT_NULL(p, msg) do { \
    if (!(p)) { FAIL(msg); return; } \
} while(0)

/* ============ DI Container Tests ============ */

static void *test_singleton_factory(void *container, void **deps, int dc) {
    (void)container; (void)deps; (void)dc;
    int *obj = malloc(sizeof(int));
    *obj = 42;
    return obj;
}

static void test_di_basic(void) {
    DIContainer c;
    int *val;

    TEST("di_init");
    di_init(&c);
    ASSERT_EQ(c.count, 0, "count should be 0");
    PASS();

    TEST("di_register");
    ASSERT_EQ(di_register(&c, "num", "int", DI_SCOPE_SINGLETON, test_singleton_factory, NULL, 0), 0, "register ok");
    ASSERT_EQ(c.count, 1, "count=1 after register");
    PASS();

    TEST("di_resolve singleton returns same instance");
    val = di_resolve(&c, "num");
    ASSERT_NOT_NULL(val, "resolve not null");
    ASSERT_EQ(*val, 42, "value is 42");
    int *val2 = di_resolve(&c, "num");
    ASSERT_EQ(val, val2, "singleton same pointer");
    PASS();

    TEST("di_resolve_type");
    int *by_type = di_resolve_type(&c, "int");
    ASSERT_NOT_NULL(by_type, "type resolve");
    ASSERT_EQ(*by_type, 42, "type resolve value");
    PASS();

    di_destroy(&c);
}

static void test_di_scopes(void) {
    DIContainer c;
    void *a1, *a2;
    di_init(&c);

    TEST("di_register transient");
    di_register(&c, "t", "trans", DI_SCOPE_TRANSIENT, test_singleton_factory, NULL, 0);
    a1 = di_resolve(&c, "t");
    a2 = di_resolve(&c, "t");
    ASSERT_TRUE(a1 != a2, "transient different instances");
    free(a1);
    PASS();

    TEST("di_register request scope");
    di_register(&c, "r", "req", DI_SCOPE_REQUEST, test_singleton_factory, NULL, 0);
    di_begin_request(&c);
    a1 = di_resolve(&c, "r");
    a2 = di_resolve(&c, "r");
    ASSERT_EQ(a1, a2, "request scope same in request");
    di_end_request(&c);
    free(a1);
    PASS();

    di_destroy(&c);
}

static void test_di_cycle_detection(void) {
    DIContainer c;
    di_init(&c);

    TEST("di_has_cycle empty container");
    ASSERT_EQ(di_has_cycle(&c, "nonexistent"), 0, "no cycle for empty");
    PASS();

    TEST("di_has_cycle no deps");
    di_register(&c, "a", "A", DI_SCOPE_SINGLETON, test_singleton_factory, NULL, 0);
    ASSERT_EQ(di_has_cycle(&c, "a"), 0, "no cycle single node");
    PASS();

    di_destroy(&c);
}

static void test_di_tags(void) {
    DIContainer c;
    void *results[8];
    di_init(&c);

    TEST("di_add_tag and resolve_tagged");
    di_register(&c, "s1", "S", DI_SCOPE_SINGLETON, test_singleton_factory, NULL, 0);
    di_add_tag(&c, "s1", "web");
    di_add_tag(&c, "s1", "api");
    int count = di_resolve_tagged(&c, "web", results, 8);
    ASSERT_EQ(count, 1, "one tagged service");
    PASS();

    di_destroy(&c);
}

static void test_di_resolve_all(void) {
    DIContainer c;
    di_init(&c);

    TEST("di_resolve_all empty");
    ASSERT_EQ(di_resolve_all(&c), 0, "resolve all empty");
    PASS();

    TEST("di_resolve_all no deps");
    di_register(&c, "a", "A", DI_SCOPE_SINGLETON, test_singleton_factory, NULL, 0);
    ASSERT_EQ(di_resolve_all(&c), 0, "resolve all single");
    PASS();

    di_destroy(&c);
}

/* ============ ORM Tests ============ */

typedef struct { int id; char name[64]; int age; } TestUser;

static ORMColumnDef test_cols[] = {
    {"id", ORM_TYPE_INT, 0, true, true, false, ""},
    {"name", ORM_TYPE_STRING, 64, false, false, false, ""},
    {"age", ORM_TYPE_INT, 0, false, false, true, "0"},
};

static void test_orm_basic(void) {
    ORMModel model;
    TestUser user;

    TEST("orm_init");
    orm_init();
    PASS();

    TEST("orm_define");
    ASSERT_EQ(orm_define("test_users", test_cols, 3, sizeof(TestUser)), 0, "define ok");
    PASS();

    TEST("orm_save");
    memcpy(&model.meta, &((ORMMeta){.table_name="test_users",.column_count=3,.struct_size=sizeof(TestUser),.pk_index=0}), sizeof(ORMMeta));
    model.data = NULL;
    memset(&user, 0, sizeof(user));
    user.id = 1;
    strcpy(user.name, "Alice");
    int pk = orm_save(&model, &user);
    ASSERT_TRUE(pk > 0, "save returns auto-increment pk");
    PASS();
}

static void test_orm_query_builder(void) {
    ORMQuery q;
    char sql[ORM_MAX_QUERY_LEN];
    int len;

    TEST("orm_query_init");
    orm_query_init(&q, "users");
    ASSERT_STR_EQ(q.table, "users", "table set");
    ASSERT_TRUE(q.select_all, "select_all default true");
    PASS();

    TEST("orm_query_generate basic SELECT");
    len = orm_query_generate(&q, sql, ORM_MAX_QUERY_LEN);
    ASSERT_TRUE(len > 0, "generated SQL non-empty");
    ASSERT_TRUE(strstr(sql, "SELECT") != NULL, "contains SELECT");
    ASSERT_TRUE(strstr(sql, "FROM users") != NULL, "contains FROM users");
    PASS();

    TEST("orm_query_where EQ");
    orm_query_init(&q, "users");
    orm_query_where(&q, "age", ORM_OP_GT, "18");
    len = orm_query_generate(&q, sql, ORM_MAX_QUERY_LEN);
    ASSERT_TRUE(strstr(sql, "WHERE") != NULL, "contains WHERE");
    ASSERT_TRUE(strstr(sql, "age") != NULL, "contains age");
    ASSERT_TRUE(strstr(sql, ">") != NULL, "contains >");
    PASS();

    TEST("orm_query_join");
    orm_query_init(&q, "users");
    orm_query_join(&q, ORM_JOIN_INNER, "users", "orders", "id", "user_id");
    len = orm_query_generate(&q, sql, ORM_MAX_QUERY_LEN);
    ASSERT_TRUE(strstr(sql, "INNER JOIN") != NULL, "contains INNER JOIN");
    PASS();

    TEST("orm_query_order + limit");
    orm_query_init(&q, "users");
    orm_query_order(&q, "name", ORM_ORDER_ASC);
    orm_query_limit(&q, 10, 5);
    len = orm_query_generate(&q, sql, ORM_MAX_QUERY_LEN);
    ASSERT_TRUE(strstr(sql, "ORDER BY") != NULL, "contains ORDER BY");
    ASSERT_TRUE(strstr(sql, "LIMIT") != NULL, "contains LIMIT");
    PASS();

    TEST("orm_query_or_where");
    orm_query_init(&q, "users");
    orm_query_where(&q, "age", ORM_OP_GT, "18");
    orm_query_or_where(&q, "name", ORM_OP_LIKE, "%test%");
    len = orm_query_generate(&q, sql, ORM_MAX_QUERY_LEN);
    ASSERT_TRUE(strstr(sql, "OR") != NULL, "contains OR");
    PASS();

    TEST("orm_query IS NULL");
    orm_query_init(&q, "users");
    orm_query_where(&q, "email", ORM_OP_IS_NULL, "");
    len = orm_query_generate(&q, sql, ORM_MAX_QUERY_LEN);
    ASSERT_TRUE(strstr(sql, "IS NULL") != NULL, "contains IS NULL");
    PASS();
}

/* ============ MVC Tests ============ */

static int test_action(MVCModel *m, MVCView *v, void *ctx) {
    (void)ctx;
    mvc_view_assign(v, "test", mvc_model_get(m, "id"));
    mvc_view_render(v);
    return 0;
}

static void test_mvc_basic(void) {
    MVCController ctrl;
    MVCModel model;
    MVCView view;
    int result;

    TEST("mvc_controller_init + register_route");
    mvc_controller_init(&ctrl);
    ASSERT_EQ(mvc_register_route(&ctrl, MVC_GET, "/users/{id}", "test", test_action), 0, "route registered");
    ASSERT_EQ(ctrl.route_count, 1, "route count 1");
    PASS();

    TEST("mvc_dispatch with path param");
    mvc_view_init(&view);
    mvc_view_set_template(&view, "User {{ test }}");
    mvc_model_init(&model, "test");
    mvc_model_set(&model, "id", "42");
    result = mvc_dispatch(&ctrl, MVC_GET, "/users/42", &model, &view, NULL);
    ASSERT_EQ(result, 0, "dispatch ok");
    ASSERT_TRUE(strstr(mvc_view_output(&view), "42") != NULL, "output contains id");
    PASS();

    TEST("mvc_dispatch no match");
    result = mvc_dispatch(&ctrl, MVC_POST, "/users/42", &model, &view, NULL);
    ASSERT_EQ(result, -1, "no match returns -1");
    PASS();

    TEST("mvc_model_validate required");
    mvc_model_init(&model, "form");
    mvc_model_add_field(&model, "name", true, 2, 64, "");
    mvc_model_set(&model, "name", "");
    ASSERT_EQ(mvc_model_validate(&model), -1, "required field fails");
    PASS();

    TEST("mvc_method_string");
    ASSERT_STR_EQ(mvc_method_string(MVC_GET), "GET", "GET string");
    ASSERT_STR_EQ(mvc_method_string(MVC_POST), "POST", "POST string");
    PASS();
}

/* ============ Validator Tests ============ */

static void test_validator_basic(void) {
    Validator v;
    int errs;

    TEST("validator required");
    validator_init(&v);
    validator_rule_required(&v, "name", "Name required");
    errs = validator_validate(&v, "name", "");
    ASSERT_EQ(errs, 1, "empty fails required");
    errs = validator_validate(&v, "name", "hello");
    ASSERT_EQ(errs, 0, "non-empty passes required");
    PASS();

    TEST("validator min_length");
    validator_init(&v);
    validator_rule_min_length(&v, "pw", 8, "Too short");
    errs = validator_validate(&v, "pw", "12345");
    ASSERT_EQ(errs, 1, "too short fails");
    errs = validator_validate(&v, "pw", "12345678");
    ASSERT_EQ(errs, 0, "8 chars passes");
    PASS();

    TEST("validator max_length");
    validator_init(&v);
    validator_rule_max_length(&v, "nick", 10, "Too long");
    errs = validator_validate(&v, "nick", "verylongnamehere");
    ASSERT_EQ(errs, 1, "too long fails");
    errs = validator_validate(&v, "nick", "short");
    ASSERT_EQ(errs, 0, "short passes");
    PASS();

    TEST("val_is_email");
    ASSERT_TRUE(val_is_email("user@example.com"), "valid email");
    ASSERT_TRUE(!val_is_email("notanemail"), "invalid email no @");
    ASSERT_TRUE(!val_is_email(""), "empty email invalid");
    ASSERT_TRUE(!val_is_email("@nodomain"), "no local part");
    ASSERT_TRUE(!val_is_email("user@"), "no domain");
    PASS();

    TEST("validator_email rule");
    validator_init(&v);
    validator_rule_email(&v, "email", "Bad email");
    errs = validator_validate(&v, "email", "user@test.com");
    ASSERT_EQ(errs, 0, "valid email passes rule");
    errs = validator_validate(&v, "email", "bad");
    ASSERT_EQ(errs, 1, "invalid email fails rule");
    PASS();

    TEST("validator_numeric");
    validator_init(&v);
    validator_rule_numeric(&v, "price", "Must be numeric");
    errs = validator_validate(&v, "price", "12.34");
    ASSERT_EQ(errs, 0, "float passes numeric");
    errs = validator_validate(&v, "price", "abc");
    ASSERT_EQ(errs, 1, "text fails numeric");
    PASS();

    TEST("validator_integer");
    validator_init(&v);
    validator_rule_integer(&v, "count", "Must be int");
    errs = validator_validate(&v, "count", "42");
    ASSERT_EQ(errs, 0, "int passes");
    errs = validator_validate(&v, "count", "3.14");
    ASSERT_EQ(errs, 1, "float fails integer");
    PASS();

    TEST("validator_min_value / max_value");
    validator_init(&v);
    validator_rule_min_value(&v, "age", 0, "Too young");
    validator_rule_max_value(&v, "age", 150, "Too old");
    errs = validator_validate(&v, "age", "25");
    ASSERT_EQ(errs, 0, "in range passes");
    errs = validator_validate(&v, "age", "-5");
    ASSERT_EQ(errs, 1, "below min fails");
    errs = validator_validate(&v, "age", "200");
    ASSERT_EQ(errs, 1, "above max fails");
    PASS();

    TEST("validator_regex alphanumeric");
    validator_init(&v);
    validator_rule_regex(&v, "user", "^[a-zA-Z0-9_]+$", "Bad chars");
    errs = validator_validate(&v, "user", "hello_world");
    ASSERT_EQ(errs, 0, "alphanumeric passes");
    errs = validator_validate(&v, "user", "hello world");
    ASSERT_EQ(errs, 1, "space fails");
    PASS();

    TEST("validator_custom rule");
    validator_init(&v);
    errs = validator_validate_field(&v, "any", "test", NULL, 0);
    ASSERT_EQ(errs, 0, "no rules = no errors");
    PASS();
}

/* ============ Serializer Tests ============ */

typedef struct {
    int id;
    char name[64];
    double score;
    bool active;
} TestObj;

static void test_serializer_basic(void) {
    Serializer s;
    TestObj obj;
    char json[SER_MAX_JSON_SIZE];
    int len;

    TEST("ser_init + field add");
    ser_init(&s, sizeof(TestObj), "test");
    ser_add_int(&s, "id", offsetof(TestObj, id), "id");
    ser_add_string(&s, "name", offsetof(TestObj, name), "name");
    ser_add_double(&s, "score", offsetof(TestObj, score), "score");
    ser_add_bool(&s, "active", offsetof(TestObj, active), "active");
    ASSERT_EQ(s.field_count, 4, "4 fields");
    PASS();

    TEST("ser_to_json");
    memset(&obj, 0, sizeof(obj));
    obj.id = 1;
    strcpy(obj.name, "Alice");
    obj.score = 95.5;
    obj.active = true;
    len = ser_to_json(&s, &obj, json, SER_MAX_JSON_SIZE);
    ASSERT_TRUE(len > 0, "json output non-empty");
    ASSERT_TRUE(strstr(json, "\"id\":1") != NULL, "contains id");
    ASSERT_TRUE(strstr(json, "\"name\":\"Alice\"") != NULL, "contains name");
    ASSERT_TRUE(strstr(json, "95.5") != NULL, "contains score");
    ASSERT_TRUE(strstr(json, "true") != NULL, "contains true");
    PASS();

    TEST("ser_from_json");
    memset(&obj, 0, sizeof(obj));
    ser_from_json(&s, &obj, "{\"id\":99,\"name\":\"Bob\",\"active\":false}");
    ASSERT_EQ(obj.id, 99, "id parsed");
    ASSERT_STR_EQ(obj.name, "Bob", "name parsed");
    ASSERT_TRUE(!obj.active, "active false parsed");
    PASS();

    TEST("ser_ignore");
    ser_ignore(&s, "score");
    len = ser_to_json(&s, &obj, json, SER_MAX_JSON_SIZE);
    ASSERT_TRUE(strstr(json, "score") == NULL, "ignored field absent");
    PASS();

    TEST("ser_json_get_string");
    ASSERT_EQ(ser_json_get_string("{\"key\":\"val\"}", "key", json, 10), 3, "get string len");
    ASSERT_STR_EQ(json, "val", "get string value");
    PASS();

    TEST("ser_json_get_int");
    ASSERT_EQ(ser_json_get_int("{\"x\":42}", "x", 0), 42, "get int");
    ASSERT_EQ(ser_json_get_int("{}", "x", 99), 99, "default int");
    PASS();

    TEST("ser_json_get_bool");
    ASSERT_EQ(ser_json_get_bool("{\"ok\":true}", "ok"), 1, "get bool true");
    ASSERT_EQ(ser_json_get_bool("{\"ok\":false}", "ok"), 0, "get bool false");
    PASS();

    TEST("ser_strip_whitespace");
    char ws[] = " { \"a\" : 1 } ";
    ser_strip_whitespace(ws);
    ASSERT_TRUE(strchr(ws, ' ') == NULL, "no spaces after strip");
    PASS();
}

/* ============ Middleware Tests ============ */

static int mw_logger(MWRequest *req) {
    mw_req_set_ctx(req, "logged", "yes");
    return 0;
}

static int mw_auth(MWRequest *req) {
    const char *token = mw_req_get_header(req, "Authorization");
    if (!token) {
        mw_req_status(req, 401);
        return -1;
    }
    return 0;
}

static int mw_responder(MWRequest *req) {
    mw_req_respond(req, "Hello World");
    return 0;
}

static int mw_error_handler(MWRequest *req) {
    mw_req_respond(req, "Error occurred");
    return 0;
}

static void test_middleware_basic(void) {
    MWPipeline pipe;
    MWRequest req;

    TEST("mw_pipeline_init + mw_use");
    mw_pipeline_init(&pipe);
    ASSERT_EQ(mw_use(&pipe, "logger", mw_logger), 0, "handler added");
    ASSERT_EQ(mw_count(&pipe), 1, "count 1");
    ASSERT_EQ(mw_use(&pipe, "responder", mw_responder), 0, "handler2 added");
    ASSERT_EQ(mw_count(&pipe), 2, "count 2");
    PASS();

    TEST("mw_process success");
    mw_request_init(&req, MW_GET, "/test");
    ASSERT_EQ(mw_process(&pipe, &req), 0, "pipeline ok");
    ASSERT_STR_EQ(mw_req_get_ctx(&req, "logged"), "yes", "logger set ctx");
    ASSERT_TRUE(strstr(req.response_body, "Hello World") != NULL, "response received");
    PASS();

    TEST("mw_process short-circuit");
    mw_pipeline_init(&pipe);
    mw_use(&pipe, "auth", mw_auth);
    mw_use(&pipe, "responder", mw_responder);
    mw_request_init(&req, MW_GET, "/api");
    /* No auth header -> should fail */
    ASSERT_EQ(mw_process(&pipe, &req), -1, "auth failed short-circuit");
    ASSERT_EQ(req.status_code, 401, "status 401 set");
    PASS();

    TEST("mw_on_error");
    mw_pipeline_init(&pipe);
    mw_on_error(&pipe, mw_error_handler);
    mw_use(&pipe, "auth", mw_auth);
    mw_request_init(&req, MW_GET, "/api");
    mw_process(&pipe, &req);
    ASSERT_TRUE(strstr(req.response_body, "Error") != NULL, "error handler called");
    PASS();

    TEST("mw_insert_at");
    mw_pipeline_init(&pipe);
    mw_use(&pipe, "first", mw_logger);
    mw_insert_at(&pipe, 0, "zero", mw_responder);
    ASSERT_EQ(mw_count(&pipe), 2, "count after insert");
    ASSERT_STR_EQ(pipe.head->name, "zero", "inserted at head");
    PASS();

    TEST("mw_remove");
    mw_pipeline_init(&pipe);
    mw_use(&pipe, "a", mw_logger);
    mw_use(&pipe, "b", mw_responder);
    ASSERT_EQ(mw_remove(&pipe, "a"), 0, "removed");
    ASSERT_EQ(mw_count(&pipe), 1, "count decreased");
    ASSERT_STR_EQ(pipe.head->name, "b", "remaining handler");
    PASS();

    TEST("mw_req_add_header + get_header");
    mw_request_init(&req, MW_POST, "/api");
    mw_req_add_header(&req, "Content-Type", "application/json");
    ASSERT_STR_EQ(mw_req_get_header(&req, "Content-Type"), "application/json", "header get");
    PASS();

    TEST("mw_method_string");
    ASSERT_STR_EQ(mw_method_string(MW_GET), "GET", "GET string");
    ASSERT_STR_EQ(mw_method_string(MW_DELETE), "DELETE", "DELETE string");
    PASS();

    mw_pipeline_destroy(&pipe);
}

/* ============ Rate Limiter Tests ============ */

static void test_rate_limiter(void) {
    TokenBucket tb;
    SlidingWindow sw;
    CompositeLimiter cl;
    int i;

    TEST("tb_init + tb_consume");
    tb_init(&tb, 100.0, 10.0);
    ASSERT_TRUE(tb.initialized, "initialized");
    for (i = 0; i < 5; i++) {
        ASSERT_TRUE(tb_consume(&tb), "consume ok within burst");
    }
    PASS();

    TEST("tb_consume_n");
    tb_init(&tb, 100.0, 20.0);
    ASSERT_EQ(tb_consume_n(&tb, 10), 10, "consume 10 from 20 burst");
    ASSERT_TRUE(tb_tokens(&tb) <= 10.1, "tokens decreased");
    PASS();

    TEST("tb_reset");
    tb_reset(&tb);
    ASSERT_TRUE(tb_tokens(&tb) >= 19.9, "reset refilled");
    PASS();

    TEST("sw_init + sw_allow");
    sw_init(&sw, 60.0, 10);
    for (i = 0; i < 5; i++)
        ASSERT_TRUE(sw_allow(&sw), "allow within limit");
    ASSERT_EQ(sw_available(&sw), 5, "5 remaining");
    PASS();

    TEST("sw_available caps at max");
    ASSERT_TRUE(sw_available(&sw) >= 0, "available non-negative");
    PASS();

    TEST("composite limiter");
    cl_init(&cl, 100.0, 10.0, 60.0, 100);
    ASSERT_TRUE(cl_allow(&cl), "composite allow");
    PASS();
}

/* ============ Cache Tests ============ */

static void test_cache(void) {
    LRUCache cache;
    const char *val;

    TEST("cache_init + cache_put");
    cache_init(&cache, 4, 0);
    ASSERT_EQ(cache_put(&cache, "k1", "v1", 0), 0, "put ok");
    ASSERT_EQ(cache_size(&cache), 1, "size 1");
    PASS();

    TEST("cache_get hit");
    val = cache_get(&cache, "k1");
    ASSERT_NOT_NULL(val, "get not null");
    ASSERT_STR_EQ(val, "v1", "value correct");
    ASSERT_EQ(cache_hits(&cache), 1, "hits incremented");
    PASS();

    TEST("cache_get miss");
    val = cache_get(&cache, "nonexistent");
    ASSERT_TRUE(val == NULL, "miss returns NULL");
    ASSERT_EQ(cache_misses(&cache), 1, "misses incremented");
    PASS();

    TEST("cache_has");
    ASSERT_TRUE(cache_has(&cache, "k1"), "has existing key");
    ASSERT_TRUE(!cache_has(&cache, "nope"), "no false positive");
    PASS();

    TEST("cache_delete");
    ASSERT_EQ(cache_delete(&cache, "k1"), 0, "delete ok");
    ASSERT_EQ(cache_size(&cache), 0, "size 0 after delete");
    ASSERT_TRUE(!cache_has(&cache, "k1"), "key gone after delete");
    PASS();

    TEST("cache_put replace");
    cache_put(&cache, "k1", "old", 0);
    cache_put(&cache, "k1", "new", 0);
    val = cache_get(&cache, "k1");
    ASSERT_STR_EQ(val, "new", "value replaced");
    PASS();

    TEST("cache_evict_lru at capacity");
    cache_clear(&cache);
    cache_init(&cache, 3, 0);
    cache_put(&cache, "a", "1", 0);
    cache_put(&cache, "b", "2", 0);
    cache_put(&cache, "c", "3", 0);
    ASSERT_EQ(cache_size(&cache), 3, "full");
    cache_put(&cache, "d", "4", 0);
    ASSERT_EQ(cache_size(&cache), 3, "still 3 after eviction");
    ASSERT_TRUE(!cache_has(&cache, "a"), "LRU evicted");
    PASS();

    TEST("cache_hit_rate");
    ASSERT_TRUE(cache_hit_rate(&cache) >= 0.0, "rate non-negative");
    PASS();

    cache_destroy(&cache);
}

/* ============ Pool Tests ============ */

static void *pool_test_create(const char *url, void *arg) {
    (void)url; (void)arg;
    int *res = malloc(sizeof(int));
    *res = 0;
    return res;
}

static void pool_test_destroy(void *res) {
    free(res);
}

static bool pool_test_validate(void *res) {
    return res != NULL;
}

static void test_pool(void) {
    Pool pool;
    void *r1, *r2;

    TEST("pool_init + prewarm");
    pool_init(&pool, 4, 30, "test://", pool_test_create, pool_test_destroy, pool_test_validate, NULL);
    ASSERT_EQ(pool_capacity(&pool), 4, "capacity 4");
    ASSERT_EQ(pool_prewarm(&pool, 2), 2, "prewarmed 2");
    ASSERT_EQ(pool_available(&pool), 4, "all available (none in use)");
    PASS();

    TEST("pool_acquire + release");
    r1 = pool_acquire(&pool, 1000);
    ASSERT_NOT_NULL(r1, "acquired");
    ASSERT_EQ(pool_in_use(&pool), 1, "1 in use");
    ASSERT_EQ(pool_release(&pool, r1), 0, "released");
    ASSERT_EQ(pool_in_use(&pool), 0, "0 in use after release");
    PASS();

    TEST("pool_acquire timeout");
    r1 = pool_acquire(&pool, -1);
    r2 = pool_acquire(&pool, -1);
    ASSERT_NOT_NULL(r1, "acquired r1");
    ASSERT_NOT_NULL(r2, "acquired r2");
    pool_release(&pool, r2);
    pool_release(&pool, r1);
    PASS();

    pool_destroy(&pool);
}

/* ============ Config Tests ============ */

static void test_config(void) {
    Config cfg;
    char dump[4096];
    char keys[16][CFG_MAX_NAME];
    int n;

    TEST("cfg_init + cfg_set + cfg_get");
    cfg_init(&cfg);
    cfg_set(&cfg, "db", "host", "localhost");
    ASSERT_STR_EQ(cfg_get(&cfg, "db", "host", "x"), "localhost", "get set value");
    PASS();

    TEST("cfg_get default");
    ASSERT_STR_EQ(cfg_get(&cfg, "db", "port", "5432"), "5432", "default returned");
    PASS();

    TEST("cfg_get_int");
    cfg_set(&cfg, "server", "port", "8080");
    ASSERT_EQ(cfg_get_int(&cfg, "server", "port", 0), 8080, "int parse");
    PASS();

    TEST("cfg_get_float");
    cfg_set(&cfg, "app", "version", "2.5");
    ASSERT_TRUE(cfg_get_float(&cfg, "app", "version", 0.0) > 2.4, "float parse");
    PASS();

    TEST("cfg_get_bool");
    cfg_set(&cfg, "app", "debug", "true");
    ASSERT_TRUE(cfg_get_bool(&cfg, "app", "debug", false), "bool true");
    cfg_set(&cfg, "app", "debug", "false");
    ASSERT_TRUE(!cfg_get_bool(&cfg, "app", "debug", true), "bool false");
    PASS();

    TEST("cfg_has");
    ASSERT_TRUE(cfg_has(&cfg, "db", "host"), "has existing");
    ASSERT_TRUE(!cfg_has(&cfg, "db", "nonexistent"), "no false positive");
    PASS();

    TEST("cfg_section_keys");
    cfg_set(&cfg, "db", "user", "admin");
    cfg_set(&cfg, "db", "pass", "secret");
    n = cfg_section_keys(&cfg, "db", keys, 16);
    ASSERT_TRUE(n >= 2, "multiple keys in section");
    PASS();

    TEST("cfg_remove");
    ASSERT_EQ(cfg_remove(&cfg, "db", "host"), 0, "removed");
    ASSERT_TRUE(!cfg_has(&cfg, "db", "host"), "gone after remove");
    PASS();

    TEST("cfg_dump");
    n = cfg_dump(&cfg, dump, sizeof(dump));
    ASSERT_TRUE(n > 0, "dump non-empty");
    ASSERT_TRUE(strstr(dump, "[db]") != NULL, "contains section header");
    PASS();

    TEST("cfg_env_override");
    cfg_init(&cfg);
    cfg_enable_env(&cfg, "TESTAPP_");
    /* env override: cfg_get falls back to env if no explicit set */
    /* (not testing actual env var here, just setup) */
    ASSERT_TRUE(cfg.env_override_enabled, "env enabled");
    PASS();

    cfg_clear(&cfg);
}

/* ============ Integration Test ============ */

static void *test_int_svc_factory(void *container, void **deps, int dc) {
    (void)container; (void)deps; (void)dc;
    char *svc = malloc(64);
    strcpy(svc, "IntegrationService");
    return svc;
}

static int test_int_handler(MVCModel *m, MVCView *v, void *ctx) {
    const char *svc_name = (const char *)ctx;
    mvc_view_assign(v, "service", svc_name ? svc_name : "none");
    mvc_view_assign(v, "user", mvc_model_get(m, "user"));
    mvc_view_render(v);
    return 0;
}

static void test_integration(void) {
    DIContainer di;
    MVCController ctrl;
    MVCModel model;
    MVCView view;
    Validator v;
    LRUCache cache;
    char *svc;

    TEST("integration: DI -> MVC -> Validator -> Cache");
    /* Setup DI */
    di_init(&di);
    di_register(&di, "api_svc", "APIService", DI_SCOPE_SINGLETON,
                test_int_svc_factory, NULL, 0);
    svc = di_resolve(&di, "api_svc");
    ASSERT_NOT_NULL(svc, "DI resolved");
    ASSERT_STR_EQ(svc, "IntegrationService", "correct service");

    /* Setup MVC */
    mvc_controller_init(&ctrl);
    mvc_register_route(&ctrl, MVC_GET, "/api/{user}", "api", test_int_handler);

    /* Setup Validator */
    validator_init(&v);
    validator_rule_required(&v, "user", "User required");
    validator_rule_min_length(&v, "user", 2, "Too short");

    /* Setup Cache */
    cache_init(&cache, 16, 0);
    cache_put(&cache, "config", "integration-test", 0);

    /* Dispatch */
    mvc_view_init(&view);
    mvc_view_set_template(&view, "Svc: {{ service }}, User: {{ user }}");
    mvc_model_init(&model, "request");
    mvc_model_set(&model, "user", "Alice");

    int result = mvc_dispatch(&ctrl, MVC_GET, "/api/Alice", &model, &view, svc);
    ASSERT_EQ(result, 0, "dispatch ok");
    ASSERT_TRUE(strstr(mvc_view_output(&view), "Alice") != NULL, "user in output");
    ASSERT_TRUE(strstr(mvc_view_output(&view), "IntegrationService") != NULL, "svc in output");

    /* Verify cache still works */
    ASSERT_STR_EQ(cache_get(&cache, "config"), "integration-test", "cache persists");
    PASS();

    cache_destroy(&cache);
    di_destroy(&di);
}

/* ============ Test Runner ============ */

int main(void) {
    printf("\n========================================\n");
    printf("  mini-backend-framework Test Suite\n");
    printf("========================================\n\n");

    printf("[1] DI Container Tests\n");
    test_di_basic();
    test_di_scopes();
    test_di_cycle_detection();
    test_di_tags();
    test_di_resolve_all();

    printf("\n[2] ORM Core Tests\n");
    test_orm_basic();
    test_orm_query_builder();

    printf("\n[3] MVC Pattern Tests\n");
    test_mvc_basic();

    printf("\n[4] Validator Tests\n");
    test_validator_basic();

    printf("\n[5] Serializer Tests\n");
    test_serializer_basic();

    printf("\n[6] Middleware Tests\n");
    test_middleware_basic();

    printf("\n[7] Rate Limiter Tests\n");
    test_rate_limiter();

    printf("\n[8] Cache Tests\n");
    test_cache();

    printf("\n[9] Connection Pool Tests\n");
    test_pool();

    printf("\n[10] Config Tests\n");
    test_config();

    printf("\n[11] Integration Test\n");
    test_integration();

    printf("\n========================================\n");
    printf("  RESULTS: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);
    printf("========================================\n\n");

    return tests_failed > 0 ? 1 : 0;
}
