#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * crypto_utils.h -- Cryptographic Utilities
 *
 * Knowledge Layers:
 *   L1: Definitions -- hash types, KDF params, secure buffer
 *   L2: Core Concepts -- one-way hashing, salting, key stretching
 *   L4: Standards -- PBKDF2 (RFC 2898), HMAC-SHA256 (RFC 2104)
 *   L5: Algorithms -- PBKDF2, HMAC, constant-time compare
 *   L7: Applications -- password hashing for auth systems
 */

#define CRYPTO_SALT_LEN          32
#define CRYPTO_HASH_LEN          32
#define CRYPTO_PEPPER_LEN        32
#define CRYPTO_MAX_PASSWORD_LEN  256
#define CRYPTO_MAX_B64_LEN       512
#define CRYPTO_PBKDF2_ITERATIONS 100000

typedef enum {
    CRYPTO_HASH_SHA256 = 0,
    CRYPTO_HASH_SHA384,
    CRYPTO_HASH_SHA512
} CryptoHashAlgo;

typedef struct {
    uint8_t  salt[CRYPTO_SALT_LEN];
    uint8_t  pepper[CRYPTO_PEPPER_LEN];
    uint32_t iterations;
    size_t   salt_len;
    size_t   pepper_len;
    CryptoHashAlgo hash_algo;
} CryptoKdfConfig;

typedef struct {
    char storage_string[CRYPTO_MAX_B64_LEN * 2 + 128];
    CryptoHashAlgo algo;
    uint32_t iterations;
    uint8_t salt[CRYPTO_SALT_LEN];
    uint8_t hash[CRYPTO_HASH_LEN];
    size_t  salt_len;
    size_t  hash_len;
} CryptoPasswordHash;

typedef struct {
    uint8_t *data;
    size_t   len;
    int      locked;
} CryptoSecureBuffer;

int crypto_sha256(const uint8_t *data, size_t len,
                  uint8_t *out, size_t *out_len);

int crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t *out, size_t *out_len);

int crypto_const_time_compare(const uint8_t *a, const uint8_t *b, size_t len);

void crypto_kdf_init(CryptoKdfConfig *config,
                     const uint8_t *pepper, size_t pepper_len);

int crypto_generate_salt(uint8_t *salt, size_t salt_len);

int crypto_pbkdf2(const uint8_t *password, size_t password_len,
                  const uint8_t *salt, size_t salt_len,
                  uint32_t iterations,
                  uint8_t *dk_out, size_t dk_len);

int crypto_hash_password(const CryptoKdfConfig *config,
                         const char *password,
                         CryptoPasswordHash *ph_out);

int crypto_verify_password(const CryptoKdfConfig *config,
                           const char *password,
                           const CryptoPasswordHash *ph);

int crypto_parse_mcf(const char *mcf_string, CryptoPasswordHash *ph_out);

CryptoSecureBuffer crypto_secure_alloc(size_t len);
void crypto_secure_free(CryptoSecureBuffer *buf);

int crypto_base64_encode(const uint8_t *src, size_t src_len,
                         char *dst, size_t dst_size);
int crypto_base64_decode(const char *src,
                         uint8_t *dst, size_t *dst_len);

const char *crypto_hash_algo_name(CryptoHashAlgo algo);

#endif /* CRYPTO_UTILS_H */
