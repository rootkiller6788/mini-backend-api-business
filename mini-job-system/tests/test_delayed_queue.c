#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "delayed_queue.h"

static int tests_run   = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  TEST %s... ", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); return; } while(0)
#define CHECK(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

static void dummy_cb(void *ud) { (void)ud; }

static void test_create_destroy(void)
{
    TEST("create/destroy");
    delayed_queue_t *dq = delayed_queue_create(16, 0);
    CHECK(dq != NULL, "create failed");
    CHECK(dq_is_empty(dq), "new queue should be empty");
    CHECK(dq_size(dq) == 0, "new queue size should be 0");
    delayed_queue_destroy(dq);
    PASS();
}

static void test_enqueue_dequeue(void)
{
    TEST("enqueue/dequeue");
    delayed_queue_t *dq = delayed_queue_create(16, 0);
    dq_enqueue(dq, 1, "task1", dummy_cb, NULL, 100);
    CHECK(dq_size(dq) == 1, "size should be 1 after enqueue");

    time_t future = time(NULL) + 10;
    dq_entry_t entry;
    int d = dq_dequeue(dq, future, &entry);
    CHECK(d == 1, "should dequeue with future time");
    CHECK(entry.id == 1, "dequeued id should match");
    CHECK(dq_is_empty(dq), "queue should be empty after dequeue");
    delayed_queue_destroy(dq);
    PASS();
}

static void test_dequeue_not_ready(void)
{
    TEST("dequeue not ready");
    delayed_queue_t *dq = delayed_queue_create(16, 0);
    dq_enqueue(dq, 1, "task1", dummy_cb, NULL, 3600000); /* 1 hour */

    dq_entry_t entry;
    int d = dq_dequeue(dq, time(NULL), &entry);
    CHECK(d == 0, "should not dequeue if fire time not reached");
    delayed_queue_destroy(dq);
    PASS();
}

static void test_peek(void)
{
    TEST("peek");
    delayed_queue_t *dq = delayed_queue_create(16, 0);
    dq_enqueue(dq, 1, "t1", dummy_cb, NULL, 100);
    dq_enqueue(dq, 2, "t2", dummy_cb, NULL, 200);

    dq_entry_t e;
    int ok = dq_peek(dq, &e);
    CHECK(ok, "peek should succeed");
    CHECK(e.id == 1, "should peek earliest entry");
    CHECK(dq_size(dq) == 2, "peek should not remove");
    delayed_queue_destroy(dq);
    PASS();
}

static void test_cancel(void)
{
    TEST("cancel");
    delayed_queue_t *dq = delayed_queue_create(16, 0);
    dq_enqueue(dq, 1, "t1", dummy_cb, NULL, 100);
    dq_enqueue(dq, 2, "t2", dummy_cb, NULL, 200);

    int r = dq_cancel(dq, 1);
    CHECK(r == 0, "cancel should return 0");
    CHECK(dq_size(dq) == 2, "cancel just marks, size unchanged");

    time_t future = time(NULL) + 1000;
    dq_entry_t e;
    int d = dq_dequeue(dq, future, &e);
    CHECK(d == 1, "should get one non-cancelled entry");
    CHECK(e.id == 2, "should get id=2 (cancelled was skipped)");
    delayed_queue_destroy(dq);
    PASS();
}

static void test_dedup(void)
{
    TEST("dedup by key");
    delayed_queue_t *dq = delayed_queue_create(16, 1);
    dq_enqueue(dq, 1, "key_x", dummy_cb, NULL, 100);
    dq_enqueue(dq, 2, "key_x", dummy_cb, NULL, 200);
    CHECK(dq_size(dq) == 1, "dedup should keep only 1 entry");
    delayed_queue_destroy(dq);
    PASS();
}

static void test_delay_levels(void)
{
    TEST("delay levels enum");
    CHECK(DQ_LEVEL_1S == 1000, "1s level");
    CHECK(DQ_LEVEL_10S == 10000, "10s level");
    CHECK(DQ_LEVEL_1MIN == 60000, "1min level");
    CHECK(DQ_LEVEL_10MIN == 600000, "10min level");
    CHECK(DQ_LEVEL_1HR == 3600000, "1hr level");
    dq_enqueue_level(NULL, 1, "k", dummy_cb, NULL, DQ_LEVEL_1S);
    PASS();
}

static void test_enqueue_null_cb(void)
{
    TEST("enqueue null callback");
    delayed_queue_t *dq = delayed_queue_create(16, 0);
    int r = dq_enqueue(dq, 1, "t", NULL, NULL, 100);
    CHECK(r == -1, "should reject NULL callback");
    delayed_queue_destroy(dq);
    PASS();
}

static void test_heap_order(void)
{
    TEST("heap order (earliest first)");
    delayed_queue_t *dq = delayed_queue_create(32, 0);
    /* Use ms delays >= 1000 so fire_time differs after /1000 truncation */
    dq_enqueue(dq, 3, "c", dummy_cb, NULL, 3000);
    dq_enqueue(dq, 1, "a", dummy_cb, NULL, 1000);
    dq_enqueue(dq, 2, "b", dummy_cb, NULL, 2000);

    time_t future = time(NULL) + 1000;
    dq_entry_t e;
    CHECK(dq_dequeue(dq, future, &e), "dequeue 1");
    CHECK(e.id == 1, "first should be id=1");
    CHECK(dq_dequeue(dq, future, &e), "dequeue 2");
    CHECK(e.id == 2, "second should be id=2");
    CHECK(dq_dequeue(dq, future, &e), "dequeue 3");
    CHECK(e.id == 3, "third should be id=3");
    delayed_queue_destroy(dq);
    PASS();
}

int main(void)
{
    printf("=== Delayed Queue Tests ===\n\n");

    test_create_destroy();
    test_enqueue_dequeue();
    test_dequeue_not_ready();
    test_peek();
    test_cancel();
    test_dedup();
    test_delay_levels();
    test_enqueue_null_cb();
    test_heap_order();

    printf("\n=== Results: %d run, %d passed, %d failed ===\n",
           tests_run, tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
