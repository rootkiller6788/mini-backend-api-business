#include "rate_limiter.h"
#include <stdio.h>
#include <string.h>

static void print_headers(const RateLimitResult *res) {
    char limit_hdr[RATE_MAX_HEADER_LEN];
    char remaining_hdr[RATE_MAX_HEADER_LEN];
    char reset_hdr[RATE_MAX_HEADER_LEN];
    rate_generate_headers(res, limit_hdr, sizeof(limit_hdr),
                          remaining_hdr, sizeof(remaining_hdr),
                          reset_hdr, sizeof(reset_hdr));
    printf("    X-RateLimit-Limit:     %s\n", limit_hdr);
    printf("    X-RateLimit-Remaining: %s\n", remaining_hdr);
    printf("    X-RateLimit-Reset:     %s\n", reset_hdr);
}

int main(void) {
    RateLimiter rl;
    RateLimitResult res;
    int i;

    rate_limiter_init(&rl, 100, 60);

    printf("=== Rate Limiter Demo ===\n\n");

    printf("--- Fixed Window (per-user, limit=5, window=60s) ---\n\n");

    for (i = 1; i <= 7; i++) {
        res = rate_limiter_check(&rl, "user:alice", RATE_SCOPE_USER,
                                 RATE_ALGO_FIXED_WINDOW, 5, 60);
        printf("[Request %d] user:alice -> %s (remaining=%u)\n",
               i, res.allowed ? "ALLOW" : "DENY", res.remaining);
    }
    print_headers(&res);

    printf("\n--- Sliding Window (per-IP, limit=3, window=10s) ---\n\n");

    for (i = 1; i <= 5; i++) {
        res = rate_limiter_check(&rl, "ip:10.0.0.42", RATE_SCOPE_IP,
                                 RATE_ALGO_SLIDING_WINDOW, 3, 10);
        printf("[Request %d] ip:10.0.0.42 -> %s (remaining=%u)\n",
               i, res.allowed ? "ALLOW" : "DENY", res.remaining);
    }

    printf("\n--- Token Bucket (per-API, limit=4, window=2s) ---\n\n");

    for (i = 1; i <= 6; i++) {
        res = rate_limiter_check(&rl, "api:/users/create", RATE_SCOPE_API,
                                 RATE_ALGO_TOKEN_BUCKET, 4, 2);
        printf("[Request %d] api:/users/create -> %s (remaining=%u)\n",
               i, res.allowed ? "ALLOW" : "DENY", res.remaining);
    }

    printf("\n--- Multi-Limiter Check ---\n\n");

    {
        const char *keys[2] = {"user:bob", "ip:192.168.1.1"};
        RateScope scopes[2] = {RATE_SCOPE_USER, RATE_SCOPE_IP};
        RateAlgo algos[2] = {RATE_ALGO_FIXED_WINDOW, RATE_ALGO_FIXED_WINDOW};
        uint32_t limits[2] = {3, 10};
        uint32_t windows[2] = {10, 10};
        RateLimitResult results[2];
        int all_ok;

        for (i = 1; i <= 5; i++) {
            all_ok = rate_limiter_check_multi(&rl, keys, scopes, algos,
                                              limits, windows, 2, results);
            printf("[Request %d] bob+IP -> %s "
                   "(user_rem=%u ip_rem=%u)\n",
                   i, all_ok ? "ALLOW" : "DENY",
                   results[0].remaining, results[1].remaining);
        }
    }

    printf("\n--- Redis/Lua Simulation (Distributed Limiter) ---\n\n");

    for (i = 1; i <= 5; i++) {
        int ok = rate_redis_sim_acquire("dist:user:charlie", 3, 60);
        printf("[Request %d] Redis sim dist:user:charlie -> %s\n",
               i, ok ? "ALLOW" : "DENY");
    }

    printf("\n--- Rate Limit Entry Dump ---\n\n");
    {
        size_t j;
        for (j = 0; j < rl.entry_count && j < 3; j++) {
            rate_dump_entry(&rl.entries[j]);
        }
    }

    printf("\n--- Cleanup Old Entries ---\n");
    rate_limiter_cleanup(&rl);
    printf("Entries after cleanup: %zu\n", rl.entry_count);

    printf("\n=== All Rate Limiter tests completed ===\n");
    return 0;
}
