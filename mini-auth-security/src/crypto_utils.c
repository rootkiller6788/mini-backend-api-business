#include "crypto_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

/* ========================================================================
 * L2+L5: SHA-256 one-shot hash (FIPS 180-4)
 *
 * Algorithm:
 *   1. Pad message to 512-bit multiple (append 1, 0s, 64-bit length)
 *   2. Initialize H0..H7 from SHA-256 IV constants
 *   3. For each 512-bit block: expand 16 words to 64, 64 compression rounds
 *   4. Output H0||H1||...||H7  (256 bits = 32 bytes)
 *
 * Complexity: O(n) time, O(1) space
 * ======================================================================== */
int crypto_sha256(const uint8_t *data, size_t len,
                  uint8_t *out, size_t *out_len) {
    if (!data || !out || !out_len) return -1;
    if (len == 0) {
        SHA256((const unsigned char *)"", 0, out);
        *out_len = 32;
        return 0;
    }
    SHA256(data, len, out);
    *out_len = 32;
    return 0;
}

/* ========================================================================
 * L5: HMAC-SHA256 (RFC 2104)
 *
 * Construction:
 *   HMAC(K, m) = H( (K' XOR opad) || H( (K' XOR ipad) || m ) )
 *
 * where K' = K padded to 64 bytes (or SHA256(K) if |K| > 64),
 * opad = 0x5C repeated, ipad = 0x36 repeated.
 *
 * Provable Security (Bellare-Canetti-Krawczyk 1996):
 *   HMAC is a secure PRF if the underlying compression function is PRF.
 *   Remains secure even if hash shows collision weaknesses (e.g., HMAC-MD5).
 *
 * Complexity: O(n) time, O(1) space
 * ======================================================================== */
int crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t *out, size_t *out_len) {
    unsigned int result_len = 32;
    if (!key || !data || !out || !out_len) return -1;
    HMAC(EVP_sha256(), key, (int)key_len,
         data, data_len, out, &result_len);
    *out_len = (size_t)result_len;
    return 0;
}

/* ========================================================================
 * L4: Constant-time memory comparison
 *
 * Theorem (Kocher 1996): Timing variance in cryptographic operations
 *   leaks secret data through side channels.
 *
 * Implementation:
 *   - All bytes compared regardless of differences (no early exit)
 *   - XOR accumulation: diff |= a[i] XOR b[i]
 *   - volatile prevents compiler from optimizing to early-exit memcmp()
 *
 * Uses: password hash verification, MAC validation, token comparison
 *
 * Complexity: O(n) time, O(1) space
 * ======================================================================== */
int crypto_const_time_compare(const uint8_t *a, const uint8_t *b, size_t len) {
    volatile uint8_t diff = 0;
    size_t i;
    if (!a || !b) return (a == b) ? 1 : 0;
    for (i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return (diff == 0) ? 1 : 0;
}

/* ========================================================================
 * L2: KDF Configuration initialization
 *
 * Defaults per NIST SP 800-132 / OWASP 2024:
 *   - 100,000 PBKDF2 iterations (minimum recommended)
 *   - SHA-256 as PRF
 * ======================================================================== */
void crypto_kdf_init(CryptoKdfConfig *config,
                     const uint8_t *pepper, size_t pepper_len) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->iterations = CRYPTO_PBKDF2_ITERATIONS;
    config->hash_algo = CRYPTO_HASH_SHA256;
    config->salt_len = 0;
    config->pepper_len = 0;
    if (pepper && pepper_len > 0 && pepper_len <= CRYPTO_PEPPER_LEN) {
        memcpy(config->pepper, pepper, pepper_len);
        config->pepper_len = pepper_len;
    }
}

/* ========================================================================
 * L5: Cryptographically secure salt generation
 *
 * NIST SP 800-63B Section 5.1.1.2 requirements:
 *   1. At least 32 bits of random salt
 *   2. Unique per credential
 *   3. Approved random bit generator
 *
 * This implementation requests 256 bits (32 bytes) from OS CSPRNG.
 *
 * Why salt? Prevents rainbow table attacks. Salt is stored in plaintext
 * alongside the hash -- it does not need to be secret.
 * ======================================================================== */
int crypto_generate_salt(uint8_t *salt, size_t salt_len) {
    if (!salt || salt_len == 0) return -1;
    if (salt_len > CRYPTO_SALT_LEN) salt_len = CRYPTO_SALT_LEN;
    if (RAND_bytes(salt, (int)salt_len) != 1) {
        size_t i;
        uint32_t seed = (uint32_t)time(NULL);
        for (i = 0; i < salt_len; i++) {
            seed = seed * 1103515245 + 12345;
            salt[i] = (uint8_t)(seed >> 16);
        }
        return -1;
    }
    return 0;
}

/* ========================================================================
 * L5: PBKDF2-HMAC-SHA256 (RFC 2898 Section 5.2)
 *
 * Key derivation via iterated HMAC:
 *   DK = T_1 || T_2 || ... || T_{ceil(dkLen/hLen)}
 *
 * Where each block T_i:
 *   U_1   = PRF(Password, Salt || INT_32_BE(i))
 *   U_j   = PRF(Password, U_{j-1})   for j = 2..c
 *   T_i   = U_1 XOR U_2 XOR ... XOR U_c
 *
 * PRF = HMAC-SHA256, hLen = 32, c = iteration count
 *
 * Security: key stretching makes brute-force expensive. NOT memory-hard
 * (use scrypt/Argon2 for memory-hardness). NIST/FIPS approved.
 *
 * Complexity: O(c * dkLen) time, O(hLen) space
 * ======================================================================== */
int crypto_pbkdf2(const uint8_t *password, size_t password_len,
                  const uint8_t *salt, size_t salt_len,
                  uint32_t iterations,
                  uint8_t *dk_out, size_t dk_len) {
    if (!password || !salt || !dk_out || dk_len == 0) return -1;
    if (iterations < 1 || password_len == 0) return -1;
    if (PKCS5_PBKDF2_HMAC((const char *)password, (int)password_len,
                          salt, (int)salt_len, (int)iterations,
                          EVP_sha256(), (int)dk_len, dk_out) != 1) {
        return -1;
    }
    return 0;
}

/* ========================================================================
 * L2: Base64 encoding (RFC 4648, standard alphabet A-Za-z0-9+/)
 *
 * Every 3 input bytes -> 4 output characters. Padding with '='.
 * ======================================================================== */
static const char B64_STD[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int crypto_base64_encode(const uint8_t *src, size_t src_len,
                         char *dst, size_t dst_size) {
    size_t i, j, mod;
    if (!src || !dst || dst_size < 4) return -1;
    for (i = 0, j = 0; i < src_len; i += 3) {
        uint32_t a = src[i];
        uint32_t b = (i + 1 < src_len) ? src[i + 1] : 0;
        uint32_t c = (i + 2 < src_len) ? src[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        if (j + 4 >= dst_size) return -1;
        dst[j++] = B64_STD[(triple >> 18) & 63];
        dst[j++] = B64_STD[(triple >> 12) & 63];
        dst[j++] = B64_STD[(triple >> 6) & 63];
        dst[j++] = B64_STD[triple & 63];
    }
    mod = src_len % 3;
    if (mod == 1 && j >= 2) { dst[j - 2] = '='; dst[j - 1] = '='; }
    else if (mod == 2 && j >= 1) { dst[j - 1] = '='; }
    dst[j] = '\0';
    return 0;
}

/* ========================================================================
 * L2: Base64 decode (RFC 4648)
 * ======================================================================== */
int crypto_base64_decode(const char *src, uint8_t *dst, size_t *dst_len) {
    static int8_t dec[256];
    static int init = 0;
    size_t i, j, len, pad = 0;
    if (!src || !dst || !dst_len) return -1;
    if (!init) {
        memset(dec, -1, sizeof(dec));
        for (i = 0; i < 64; i++) dec[(int)B64_STD[i]] = (int8_t)i;
        dec[(int)'='] = -2;
        init = 1;
    }
    len = strlen(src);
    while (len > 0 && src[len - 1] == '=') { len--; pad++; }
    for (i = 0, j = 0; i < len; ) {
        int8_t v0 = (i < len) ? dec[(int)(uint8_t)src[i++]] : -1;
        int8_t v1 = (i < len) ? dec[(int)(uint8_t)src[i++]] : -1;
        int8_t v2 = (i < len) ? dec[(int)(uint8_t)src[i++]] : -1;
        int8_t v3 = (i < len) ? dec[(int)(uint8_t)src[i++]] : -1;
        if (v0 < 0 || v1 < 0) return -1;
        if (v2 < -1 || v3 < -1) return -1;
        uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12)
                        | ((uint32_t)v2 << 6) | (uint32_t)v3;
        if (j < *dst_len) dst[j++] = (triple >> 16) & 255;
        if (j < *dst_len) dst[j++] = (triple >> 8) & 255;
        if (j < *dst_len) dst[j++] = triple & 255;
    }
    j -= pad;
    *dst_len = j;
    return 0;
}

/* ========================================================================
 * L7: Password hashing with Modular Crypt Format (MCF)
 *
 * Format: $pbkdf2-sha256$<iterations>$<base64_salt>$<base64_hash>
 *
 * Pepper (secret, not stored in output):
 *   - Appended to password before PBKDF2
 *   - NEVER included in the MCF output string
 *   - Stored separately: env var, HSM, vault
 *   - If DB leaks, attacker cannot crack without pepper
 *
 * Complexity: O(iterations) time
 * ======================================================================== */
int crypto_hash_password(const CryptoKdfConfig *config,
                         const char *password,
                         CryptoPasswordHash *ph_out) {
    uint8_t salt[CRYPTO_SALT_LEN];
    uint8_t combined[CRYPTO_MAX_PASSWORD_LEN + CRYPTO_PEPPER_LEN];
    uint8_t derived[CRYPTO_HASH_LEN];
    size_t combined_len, pw_len;
    char salt_b64[CRYPTO_MAX_B64_LEN];
    char hash_b64[CRYPTO_MAX_B64_LEN];

    if (!config || !password || !ph_out) return -1;
    pw_len = strlen(password);
    if (pw_len == 0 || pw_len > CRYPTO_MAX_PASSWORD_LEN) return -1;

    crypto_generate_salt(salt, CRYPTO_SALT_LEN);

    memcpy(combined, password, pw_len);
    combined_len = pw_len;
    if (config->pepper_len > 0) {
        memcpy(combined + pw_len, config->pepper, config->pepper_len);
        combined_len += config->pepper_len;
    }

    if (crypto_pbkdf2(combined, combined_len,
                      salt, CRYPTO_SALT_LEN,
                      config->iterations,
                      derived, CRYPTO_HASH_LEN) != 0) {
        memset(combined, 0, sizeof(combined));
        return -1;
    }

    crypto_base64_encode(salt, CRYPTO_SALT_LEN, salt_b64, sizeof(salt_b64));
    crypto_base64_encode(derived, CRYPTO_HASH_LEN, hash_b64, sizeof(hash_b64));

    snprintf(ph_out->storage_string, sizeof(ph_out->storage_string),
             "$pbkdf2-sha256$%u$%s$%s",
             config->iterations, salt_b64, hash_b64);

    ph_out->algo = config->hash_algo;
    ph_out->iterations = config->iterations;
    memcpy(ph_out->salt, salt, CRYPTO_SALT_LEN);
    ph_out->salt_len = CRYPTO_SALT_LEN;
    memcpy(ph_out->hash, derived, CRYPTO_HASH_LEN);
    ph_out->hash_len = CRYPTO_HASH_LEN;

    memset(combined, 0, sizeof(combined));
    memset(derived, 0, sizeof(derived));
    return 0;
}

/* ========================================================================
 * L7: Password verification against MCF-stored hash
 *
 * Steps:
 *   1. Re-derive PBKDF2(password + pepper, salt, iterations)
 *   2. Compare via constant-time comparison (timing attack defense)
 *
 * Complexity: O(iterations) time
 * ======================================================================== */
int crypto_verify_password(const CryptoKdfConfig *config,
                           const char *password,
                           const CryptoPasswordHash *ph) {
    uint8_t combined[CRYPTO_MAX_PASSWORD_LEN + CRYPTO_PEPPER_LEN];
    uint8_t derived[CRYPTO_HASH_LEN];
    size_t combined_len, pw_len;
    int result;

    if (!config || !password || !ph) return -1;
    pw_len = strlen(password);
    if (pw_len == 0 || pw_len > CRYPTO_MAX_PASSWORD_LEN) return -1;

    memcpy(combined, password, pw_len);
    combined_len = pw_len;
    if (config->pepper_len > 0) {
        memcpy(combined + pw_len, config->pepper, config->pepper_len);
        combined_len += config->pepper_len;
    }

    if (crypto_pbkdf2(combined, combined_len,
                      ph->salt, ph->salt_len,
                      ph->iterations,
                      derived, CRYPTO_HASH_LEN) != 0) {
        memset(combined, 0, sizeof(combined));
        return -1;
    }

    result = crypto_const_time_compare(derived, ph->hash, ph->hash_len);
    memset(combined, 0, sizeof(combined));
    memset(derived, 0, sizeof(derived));
    return result;
}

/* ========================================================================
 * L7: Parse Modular Crypt Format password hash string
 *
 * Tokenizes on '$':  $<algo>$<iterations>$<salt_b64>$<hash_b64>
 * ======================================================================== */
int crypto_parse_mcf(const char *mcf_string, CryptoPasswordHash *ph_out) {
    char work[CRYPTO_MAX_B64_LEN * 2 + 128];
    char *saveptr;
    char *algo_sec, *iter_str, *salt_b64, *hash_b64;
    uint8_t salt_raw[CRYPTO_SALT_LEN];
    uint8_t hash_raw[CRYPTO_HASH_LEN];
    size_t salt_len, hash_len;

    if (!mcf_string || !ph_out) return -1;
    if (mcf_string[0] != '$') return -1;

    memset(ph_out, 0, sizeof(*ph_out));
    strncpy(work, mcf_string, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    algo_sec = strtok_s(work + 1, "$", &saveptr);
    iter_str = strtok_s(NULL, "$", &saveptr);
    salt_b64 = strtok_s(NULL, "$", &saveptr);
    hash_b64 = strtok_s(NULL, "$", &saveptr);

    if (!algo_sec || !iter_str || !salt_b64 || !hash_b64) return -1;

    if (strcmp(algo_sec, "pbkdf2-sha256") == 0) {
        ph_out->algo = CRYPTO_HASH_SHA256;
    } else {
        return -1;
    }

    ph_out->iterations = (uint32_t)strtoul(iter_str, NULL, 10);
    if (ph_out->iterations < 10) return -1;

    salt_len = sizeof(salt_raw);
    if (crypto_base64_decode(salt_b64, salt_raw, &salt_len) != 0) return -1;
    if (salt_len > CRYPTO_SALT_LEN) salt_len = CRYPTO_SALT_LEN;
    memcpy(ph_out->salt, salt_raw, salt_len);
    ph_out->salt_len = salt_len;

    hash_len = sizeof(hash_raw);
    if (crypto_base64_decode(hash_b64, hash_raw, &hash_len) != 0) return -1;
    if (hash_len > CRYPTO_HASH_LEN) hash_len = CRYPTO_HASH_LEN;
    memcpy(ph_out->hash, hash_raw, hash_len);
    ph_out->hash_len = hash_len;

    snprintf(ph_out->storage_string, sizeof(ph_out->storage_string),
             "%s", mcf_string);
    return 0;
}

/* ========================================================================
 * L2: Secure buffer with memory locking (mlock)
 *
 * Prevents secrets from being paged to swap.
 * CWE-14: Compiler Removal of Code to Clear Buffers
 * CVE-2019-14899: VPN secrets leaked via swap
 * ======================================================================== */
CryptoSecureBuffer crypto_secure_alloc(size_t len) {
    CryptoSecureBuffer buf = {NULL, 0, 0};
    if (len == 0) return buf;
    buf.data = (uint8_t *)calloc(1, len);
    if (!buf.data) return buf;
    buf.len = len;
    buf.locked = 0;
#if defined(_POSIX_MEMLOCK) || defined(__linux__) || defined(__APPLE__)
    {
        extern int mlock(const void *addr, size_t length);
        if (mlock(buf.data, len) == 0) {
            buf.locked = 1;
        }
    }
#endif
    return buf;
}

/* ========================================================================
 * L2: Secure buffer deallocation with zeroization
 *
 * Overwrites buffer with zeros before freeing. Uses volatile pointer
 * to prevent dead-store elimination by compiler (per CWE-14).
 * ======================================================================== */
void crypto_secure_free(CryptoSecureBuffer *buf) {
    if (!buf || !buf->data) return;
    {
        volatile uint8_t *vp = buf->data;
        size_t i;
        for (i = 0; i < buf->len; i++) {
            vp[i] = 0;
        }
    }
#if defined(_POSIX_MEMLOCK) || defined(__linux__) || defined(__APPLE__)
    if (buf->locked) {
        extern int munlock(const void *addr, size_t length);
        munlock(buf->data, buf->len);
    }
#endif
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->locked = 0;
}

const char *crypto_hash_algo_name(CryptoHashAlgo algo) {
    switch (algo) {
    case CRYPTO_HASH_SHA256: return "SHA-256";
    case CRYPTO_HASH_SHA384: return "SHA-384";
    case CRYPTO_HASH_SHA512: return "SHA-512";
    default: return "Unknown";
    }
}