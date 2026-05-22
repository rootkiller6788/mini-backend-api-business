#ifndef JWT_AUTH_H
#define JWT_AUTH_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define JWT_MAX_HEADER_LEN   256
#define JWT_MAX_PAYLOAD_LEN  2048
#define JWT_MAX_TOKEN_LEN    4096
#define JWT_MAX_SECRET_LEN   512
#define JWT_MAX_ISS_LEN      128
#define JWT_MAX_SUB_LEN      128
#define JWT_MAX_AUD_LEN      256

typedef enum {
    JWT_ALG_NONE = 0,
    JWT_ALG_HS256,
    JWT_ALG_HS384,
    JWT_ALG_HS512,
    JWT_ALG_RS256,
    JWT_ALG_RS384,
    JWT_ALG_RS512
} JwtAlgorithm;

typedef struct {
    char header[JWT_MAX_HEADER_LEN];
    char payload[JWT_MAX_PAYLOAD_LEN];
    char signature[512];
} JwtToken;

typedef struct {
    char alg[16];
    char typ[8];
    char kid[64];
} JwtHeader;

typedef struct {
    char iss[JWT_MAX_ISS_LEN];
    char sub[JWT_MAX_SUB_LEN];
    char aud[JWT_MAX_AUD_LEN];
    int64_t exp;
    int64_t iat;
    int64_t nbf;
    char jti[64];
    char scope[256];
} JwtClaims;

typedef struct {
    uint8_t secret[JWT_MAX_SECRET_LEN];
    size_t  secret_len;
    JwtAlgorithm algorithm;
} JwtSigningKey;

typedef struct {
    JwtSigningKey key;
    int (*sign_func)(const uint8_t *data, size_t data_len,
                     const JwtSigningKey *key, uint8_t *sig_out, size_t *sig_len);
    int (*verify_func)(const uint8_t *data, size_t data_len,
                       const JwtSigningKey *key,
                       const uint8_t *sig, size_t sig_len);
} JwtEngine;

void jwt_engine_init(JwtEngine *eng, JwtAlgorithm alg,
                     const uint8_t *secret, size_t secret_len);

int  jwt_encode(JwtEngine *eng, const JwtClaims *claims,
                char *token_out, size_t token_size);

int  jwt_decode(JwtEngine *eng, const char *token,
                JwtClaims *claims_out);

int  jwt_validate_expiry(const JwtClaims *claims, time_t now);
int  jwt_validate_issuer(const JwtClaims *claims, const char *expected_iss);
int  jwt_validate_audience(const JwtClaims *claims, const char *expected_aud);

int  jwt_validate_all(JwtEngine *eng, const char *token,
                      const char *expected_iss, const char *expected_aud,
                      JwtClaims *claims_out);

int  jwt_refresh_token(JwtEngine *eng, const char *token,
                       int64_t new_expiry, char *new_token_out, size_t size);

void jwt_claims_set_defaults(JwtClaims *claims);
const char *jwt_alg_name(JwtAlgorithm alg);

int  jwt_hs256_sign(const uint8_t *data, size_t data_len,
                    const JwtSigningKey *key,
                    uint8_t *sig_out, size_t *sig_len);
int  jwt_hs256_verify(const uint8_t *data, size_t data_len,
                      const JwtSigningKey *key,
                      const uint8_t *sig, size_t sig_len);

int  jwt_rs256_sign_sim(const uint8_t *data, size_t data_len,
                        const JwtSigningKey *key,
                        uint8_t *sig_out, size_t *sig_len);
int  jwt_rs256_verify_sim(const uint8_t *data, size_t data_len,
                          const JwtSigningKey *key,
                          const uint8_t *sig, size_t sig_len);

void jwt_base64url_encode(const uint8_t *src, size_t src_len,
                          char *dst, size_t dst_size);
int  jwt_base64url_decode(const char *src,
                          uint8_t *dst, size_t *dst_len);

#endif
