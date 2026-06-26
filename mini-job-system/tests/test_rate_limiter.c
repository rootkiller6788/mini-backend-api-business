#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "rate_limiter.h"

static int tr, tp, tf;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{tp++;printf("PASS\n");}while(0)
#define F(m) do{tf++;printf("FAIL: %s\n",m);return;}while(0)
#define C(c,m) do{if(!(c)){F(m);return;}}while(0)

static void test_token_bucket_consume(void)
{
    T("token bucket consume");
    rate_limiter_t rl;
    rl_token_bucket_init(&rl, 10.0, 100.0); /* 10 tokens/sec, burst 100 */
    C(rl_consume(&rl, 1.0), "should allow 1 token");
    C(rl_consume(&rl, 50.0), "should allow 50 tokens (within burst)");
    C(rl_consume(&rl, 49.0), "should allow remaining 49");
    C(!rl_consume(&rl, 1.0), "should reject when empty");
    P();
}

static void test_token_bucket_refill(void)
{
    T("token bucket refill");
    rate_limiter_t rl;
    rl_token_bucket_init(&rl, 1000.0, 100.0); /* high rate, small burst */
    rl_consume(&rl, 100.0); /* empty it */
    rl_consume(&rl, 1.0); /* should fail */
    /* Force refill by setting last_refill to past */
    rl.impl.token.last_refill = time(NULL) - 10;
    rl_token_refill(&rl.impl.token);
    C(rl.impl.token.tokens > 0, "tokens should be refilled");
    P();
}

static void test_leaky_bucket(void)
{
    T("leaky bucket basic");
    rate_limiter_t rl;
    rl_leaky_bucket_init(&rl, 5.0, 50.0); /* 5 units/sec, capacity 50 */
    C(rl_consume(&rl, 30.0), "should allow 30 within capacity");
    C(rl_consume(&rl, 20.0), "should allow 20 more (fills to 50)");
    C(!rl_consume(&rl, 1.0), "should reject when full");
    P();
}

static void test_leaky_bucket_drain(void)
{
    T("leaky bucket drains over time");
    rate_limiter_t rl;
    rl_leaky_bucket_init(&rl, 10.0, 100.0);
    rl_consume(&rl, 100.0); /* fill completely */
    rl.impl.leaky.last_leak = time(NULL) - 5; /* 5 seconds ago */
    rl_leaky_drain(&rl.impl.leaky);
    C(rl.impl.leaky.water_level < 100.0, "should have drained some");
    C(rl.impl.leaky.water_level > 0.0, "should have some left");
    P();
}

static void test_fixed_window(void)
{
    T("fixed window counter");
    rate_limiter_t rl;
    rl_fixed_window_init(&rl, 60.0, 10.0); /* 10 per minute */
    int i, allowed = 0;
    for (i = 0; i < 15; i++) {
        if (rl_consume(&rl, 1.0)) allowed++;
    }
    C(allowed == 10, "should allow exactly 10 in window");
    P();
}

static void test_sliding_window(void)
{
    T("sliding window");
    rate_limiter_t rl;
    rl_sliding_window_init(&rl, 60.0, 100.0); /* 100 cost per 60s window */
    C(rl_consume(&rl, 60.0), "allow 60");
    C(rl_consume(&rl, 40.0), "allow 40 (exactly 100)");
    /* Now at 100, next should reject */
    C(!rl_consume(&rl, 1.0), "reject when at limit");
    P();
}

static void test_usage_ratio(void)
{
    T("usage ratio tracking");
    rate_limiter_t rl;
    rl_token_bucket_init(&rl, 10.0, 100.0);
    rl_consume(&rl, 50.0);
    double ratio = rl_usage_ratio(&rl);
    C(ratio >= 0.49 && ratio <= 0.51, "usage ~50%");
    P();
}

static void test_reset(void)
{
    T("reset limiter");
    rate_limiter_t rl;
    rl_token_bucket_init(&rl, 5.0, 50.0);
    rl_consume(&rl, 49.0);
    rl_reset(&rl);
    C(rl_consume(&rl, 50.0), "after reset, full burst available");
    P();
}

static void test_would_allow(void)
{
    T("would_allow (dry run)");
    rate_limiter_t rl;
    rl_token_bucket_init(&rl, 10.0, 100.0);
    int before = (int)rl_remaining(&rl);
    C(rl_would_allow(&rl, 1.0), "would allow");
    int after = (int)rl_remaining(&rl);
    C(before == after, "would_allow should not consume");
    P();
}

int main(void)
{
    tr = tp = tf = 0;
    printf("=== Rate Limiter Tests ===\n\n");
    test_token_bucket_consume();
    test_token_bucket_refill();
    test_leaky_bucket();
    test_leaky_bucket_drain();
    test_fixed_window();
    test_sliding_window();
    test_usage_ratio();
    test_reset();
    test_would_allow();
    printf("\n=== Results: %d run, %d passed, %d failed ===\n", tr, tp, tf);
    return tf > 0 ? 1 : 0;
}
