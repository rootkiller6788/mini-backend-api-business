#include "bloom_filter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/**
 * Bloom Filter Implementation
 *
 * Reference: Burton H. Bloom, "Space/Time Trade-offs in Hash Coding
 *            with Allowable Errors", Communications of the ACM, 1970.
 *
 * L4 Theorem — False Positive Probability:
 *   For m bits, k hash functions, n elements:
 *     p ≈ (1 - e^(-kn/m))^k
 *
 * Optimal parameters for target false-positive rate ε:
 *   m = ⌈-n·ln(ε) / (ln 2)²⌉
 *   k = ⌈(m/n)·ln(2)⌉
 *
 * Hash optimization (Kirsch-Mitzenmacher 2006):
 *   g_i(x) = h_1(x) + i·h_2(x)  for i ∈ [0, k)
 *
 * L5 Algorithm — Double Hashing Derivation:
 *   Uses MurmurHash3 finalizer on FNV-1a base hash.
 *   This provides k independent-like hash values from 2 base calculations.
 */

struct bf_bloom_filter {
    uint8_t *bits;
    size_t   bit_count;
    size_t   byte_count;
    int      hash_count;
    size_t   element_count;
};

/* --- FNV-1a 64-bit hash (L5: non-cryptographic hash function) --- */
static uint64_t bf_fnv1a(const void *data, size_t len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* --- derive two base hashes from FNV-1a --- */
static void bf_hash_pair(const void *data, size_t len,
                          uint64_t *h1, uint64_t *h2) {
    uint64_t base = bf_fnv1a(data, len);
    *h1 = base;
    /* second hash: XOR high/low halves and re-hash */
    *h2 = bf_fnv1a(&base, sizeof(base));
}

bf_bloom_filter_t *bf_create(size_t capacity, double fp_rate) {
    if (capacity == 0) capacity = BF_DEFAULT_CAPACITY;
    if (fp_rate <= 0.0 || fp_rate >= 1.0) fp_rate = BF_DEFAULT_FP_RATE;

    /* L4: m = -n·ln(ε) / (ln 2)² */
    double m_d = -(double)capacity * log(fp_rate) / (log(2.0) * log(2.0));
    size_t m = (size_t)ceil(m_d);
    if (m < 64) m = 64;

    /* L4: k = (m/n)·ln(2) */
    double k_d = ((double)m / (double)capacity) * log(2.0);
    int k = (int)ceil(k_d);
    if (k < 1) k = 1;
    if (k > BF_MAX_HASH_FUNCTIONS) k = BF_MAX_HASH_FUNCTIONS;

    return bf_create_with_params(m, k);
}

bf_bloom_filter_t *bf_create_with_params(size_t bit_count, int num_hashes) {
    if (bit_count == 0 || num_hashes <= 0) return NULL;
    if (num_hashes > BF_MAX_HASH_FUNCTIONS) num_hashes = BF_MAX_HASH_FUNCTIONS;

    bf_bloom_filter_t *bf = (bf_bloom_filter_t *)calloc(1, sizeof(*bf));
    if (!bf) return NULL;

    bf->bit_count = bit_count;
    bf->byte_count = (bit_count + 7) / 8;
    bf->hash_count = num_hashes;

    bf->bits = (uint8_t *)calloc(bf->byte_count, 1);
    if (!bf->bits) { free(bf); return NULL; }

    return bf;
}

void bf_destroy(bf_bloom_filter_t *bf) {
    if (!bf) return;
    free(bf->bits);
    free(bf);
}

int bf_add(bf_bloom_filter_t *bf, const void *data, size_t len) {
    if (!bf || !data || len == 0) return -1;

    uint64_t h1, h2;
    bf_hash_pair(data, len, &h1, &h2);

    int all_set = 1;
    for (int i = 0; i < bf->hash_count; i++) {
        /* Kirsch-Mitzenmacher: g_i = h1 + i·h2 */
        uint64_t combined = h1 + (uint64_t)i * h2;
        size_t bit_idx = (size_t)(combined % bf->bit_count);
        size_t byte_idx = bit_idx / 8;
        uint8_t mask = (uint8_t)(1U << (bit_idx % 8));

        if (!(bf->bits[byte_idx] & mask)) all_set = 0;
        bf->bits[byte_idx] |= mask;
    }

    if (!all_set) bf->element_count++;
    return 0;
}

int bf_add_string(bf_bloom_filter_t *bf, const char *str) {
    if (!bf || !str) return -1;
    return bf_add(bf, str, strlen(str));
}

int bf_contains(bf_bloom_filter_t *bf, const void *data, size_t len) {
    if (!bf || !data || len == 0) return 0;

    uint64_t h1, h2;
    bf_hash_pair(data, len, &h1, &h2);

    for (int i = 0; i < bf->hash_count; i++) {
        uint64_t combined = h1 + (uint64_t)i * h2;
        size_t bit_idx = (size_t)(combined % bf->bit_count);
        size_t byte_idx = bit_idx / 8;
        uint8_t mask = (uint8_t)(1U << (bit_idx % 8));

        if (!(bf->bits[byte_idx] & mask)) return 0; /* definitely not present */
    }
    return 1; /* possibly present */
}

int bf_contains_string(bf_bloom_filter_t *bf, const char *str) {
    if (!bf || !str) return 0;
    return bf_contains(bf, str, strlen(str));
}

void bf_clear(bf_bloom_filter_t *bf) {
    if (!bf) return;
    memset(bf->bits, 0, bf->byte_count);
    bf->element_count = 0;
}

size_t bf_bit_count(bf_bloom_filter_t *bf) {
    return bf ? bf->bit_count : 0;
}

int bf_hash_count(bf_bloom_filter_t *bf) {
    return bf ? bf->hash_count : 0;
}

size_t bf_element_count(bf_bloom_filter_t *bf) {
    return bf ? bf->element_count : 0;
}

double bf_current_fp_rate(bf_bloom_filter_t *bf) {
    if (!bf || bf->element_count == 0) return 0.0;
    /* L4: p = (1 - e^(-k·n/m))^k */
    double k = (double)bf->hash_count;
    double n = (double)bf->element_count;
    double m = (double)bf->bit_count;
    double exponent = -k * n / m;
    double inner = 1.0 - exp(exponent);
    return pow(inner, k);
}

double bf_fill_ratio(bf_bloom_filter_t *bf) {
    if (!bf || bf->bit_count == 0) return 0.0;
    size_t set_bits = 0;
    for (size_t i = 0; i < bf->byte_count; i++) {
        uint8_t b = bf->bits[i];
        /* Brian Kernighan's population count */
        while (b) { set_bits++; b &= b - 1; }
    }
    return (double)set_bits / (double)bf->bit_count;
}
