#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "cron_scheduler.h"

static int tests_run   = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %s... ", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    tests_failed++; \
    printf("FAIL: %s\n", msg); \
} while(0)

#define CHECK(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

/* Test: parse every-second cron */
static void test_parse_every_second(void)
{
    TEST("parse '*/5 * * * * *'");
    cron_expression_t expr;
    int r = cron_parse_expression(&expr, "*/5 * * * * *");
    CHECK(r == 0, "parse failed");
    CHECK(expr.fields[CRON_FIELD_SECOND].type == CRON_ENTRY_STEP, "second not step");
    CHECK(expr.fields[CRON_FIELD_MINUTE].type == CRON_ENTRY_ANY, "minute not any");
    PASS();
}

/* Test: parse 5-field cron (standard Unix) */
static void test_parse_5field(void)
{
    TEST("parse 5-field '0 0 * * *'");
    cron_expression_t expr;
    int r = cron_parse_expression(&expr, "0 0 * * *");
    CHECK(r == 0, "parse failed");
    PASS();
}

/* Test: parse specific values */
static void test_parse_specific(void)
{
    TEST("parse '30 9 15 * *'");
    cron_expression_t expr;
    int r = cron_parse_expression(&expr, "30 9 15 * *");
    CHECK(r == 0, "parse specific failed");
    PASS();
}

/* Test: field matching */
static void test_field_match(void)
{
    TEST("field match wildcard");
    cron_field_def_t f;
    cron_parse_field(&f, "*", 0, 59);
    CHECK(cron_field_matches(&f, 42), "* should match anything");

    TEST("field match specific");
    cron_parse_field(&f, "1,5,10", 0, 59);
    CHECK(cron_field_matches(&f, 5), "should match 5");
    CHECK(!cron_field_matches(&f, 7), "should not match 7");

    TEST("field match range");
    cron_parse_field(&f, "5-10", 0, 59);
    CHECK(cron_field_matches(&f, 7), "should match 7 in range");
    CHECK(!cron_field_matches(&f, 3), "should not match 3 outside range");
    PASS();
}

/* Test: scheduler create/destroy */
static void test_scheduler_lifecycle(void)
{
    TEST("scheduler create/destroy");
    cron_scheduler_t *s = cron_scheduler_create(NULL);
    CHECK(s != NULL, "create failed");
    cron_scheduler_destroy(s);
    PASS();
}

/* Test: register/unregister */
static void test_register_job(void)
{
    TEST("register job");
    cron_scheduler_t *s = cron_scheduler_create(NULL);
    int r = cron_register_job(s, 1, "0 0 * * *", NULL, NULL, 0);
    /* should fail because cb is NULL */
    CHECK(r == -1, "should reject NULL callback");
    cron_scheduler_destroy(s);
    PASS();
}

/* Test: next fire time calculation */
static void test_next_fire(void)
{
    TEST("next fire time basic");
    cron_expression_t expr;
    int r = cron_parse_expression(&expr, "0 0 * * * *");
    CHECK(r == 0, "parse failed");

    time_t now = time(NULL);
    time_t next = cron_next_fire_time(&expr, now);
    CHECK(next > now, "next fire should be in the future");
    CHECK(next != (time_t)-1, "next fire should be valid");
    PASS();
}

/* Test: cron_matches */
static void test_cron_matches(void)
{
    TEST("cron_matches");
    cron_expression_t expr;
    cron_parse_expression(&expr, "0 0 12 * * *");

    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_hour = 12; tm_val.tm_min = 0; tm_val.tm_sec = 0; tm_val.tm_mday = 1; tm_val.tm_mon = 0; tm_val.tm_wday = 0;
    CHECK(cron_matches(&expr, &tm_val), "should match noon");

    tm_val.tm_hour = 13;
    CHECK(!cron_matches(&expr, &tm_val), "should not match 1pm");
    PASS();
}

/* Test: cron_tick on empty scheduler */
static void test_cron_tick_empty(void)
{
    TEST("cron_tick on empty scheduler");
    cron_scheduler_t *s = cron_scheduler_create(NULL);
    int fired = cron_tick(s, time(NULL));
    CHECK(fired == 0, "empty scheduler should fire 0 jobs");
    cron_scheduler_destroy(s);
    PASS();
}

/* Test: activate/deactivate */
static void test_activate_job(void)
{
    TEST("activate/deactivate");
    cron_scheduler_t *s = cron_scheduler_create(NULL);
    int r = cron_activate_job(s, 999, 0);
    CHECK(r == -1, "deactivate non-existent job should return -1");
    cron_scheduler_destroy(s);
    PASS();
}

/* Test: parse invalid cron */
static void test_parse_invalid(void)
{
    TEST("parse invalid spec (too few fields)");
    cron_expression_t expr;
    int r = cron_parse_expression(&expr, "* * *");
    CHECK(r == -1, "should fail with too few fields");

    TEST("parse empty string");
    r = cron_parse_expression(&expr, "");
    CHECK(r == -1, "should fail with empty string");
    PASS();
}

int main(void)
{
    printf("=== Cron Scheduler Tests ===\n\n");

    test_parse_every_second();
    test_parse_5field();
    test_parse_specific();
    test_field_match();
    test_scheduler_lifecycle();
    test_register_job();
    test_next_fire();
    test_cron_matches();
    test_cron_tick_empty();
    test_activate_job();
    test_parse_invalid();

    printf("\n=== Results: %d run, %d passed, %d failed ===\n",
           tests_run, tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
