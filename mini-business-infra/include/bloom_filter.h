#ifndef BLOOM_FILTER_H
#define BLOOM_FILTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BF_DEFAULT_CAPACITY      10000
#define BF_DEFAULT_FP_RATE       0.01
#define BF_MAX_HASH_FUNCTIONS    32

/**
 * Bloom Filter (L5: Algorithm)
 *
 * Implements Bloom "Space/Time Trade-offs in Hash Coding with Allowable
 * Errors" (CACM 1970).
 *
 * Theorem (L4): For a Bloom filter with m bits, k hash functions,
 * and n inserted elements, the false positive probability is:
 *
 *   p ≈ (1 - e^(-kn/m))^k
 *
 * Optimal number of hash functions for target FP rate ε:
 *
 *   k = (m/n) * ln(2)
 *   m = -n * ln(ε) / (ln(2))^2
 *
 * This implementation uses the Kirsch-Mitzenmacher optimization:
 * two base hash functions h1(x) and h2(x) generate k hash functions
 * via g_i(x) = h1(x) + i * h2(x), reducing hash computation cost.
 *
 * Use case (L7): Cache penetration prevention — check Bloom filter
 * before querying backend storage for non-existent keys.
 */
typedef struct bf_bloom_filter bf_bloom_filter_t;

bf_bloom_filter_t *bf_create(size_t capacity, double fp_rate);
bf_bloom_filter_t *bf_create_with_params(size_t bit_count, int num_hashes);
void               bf_destroy(bf_bloom_filter_t *bf);

int                bf_add(bf_bloom_filter_t *bf, const void *data, size_t len);
int                bf_add_string(bf_bloom_filter_t *bf, const char *str);

int                bf_contains(bf_bloom_filter_t *bf, const void *data, size_t len);
int                bf_contains_string(bf_bloom_filter_t *bf, const char *str);

void               bf_clear(bf_bloom_filter_t *bf);

size_t             bf_bit_count(bf_bloom_filter_t *bf);
int                bf_hash_count(bf_bloom_filter_t *bf);
size_t             bf_element_count(bf_bloom_filter_t *bf);
double             bf_current_fp_rate(bf_bloom_filter_t *bf);
double             bf_fill_ratio(bf_bloom_filter_t *bf);

#ifdef __cplusplus
}
#endif

#endif
