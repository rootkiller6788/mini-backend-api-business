#include "jwt_auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/hmac.h>
#include <openssl/sha.h>

static const char B64URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void jwt_base64url_encode(const uint8_t *src, size_t src_len,
                          char *dst, size_t dst_size) {
    size_t i, j, pad;
    (void)dst_size;
    for (i = 0, j = 0; i < src_len;) {
        uint32_t a = i < src_len ? src[i++] : 0;
        uint32_t b = i < src_len ? src[i++] : 0;
        uint32_t c = i < src_len ? src[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        dst[j++] = B64URL[(triple >> 18) & 63];
        dst[j++] = B64URL[(triple >> 12) & 63];
        dst[j++] = B64URL[(triple >> 6) & 63];
        dst[j++] = B64URL[triple & 63];
    }
    pad = (3 - (src_len % 3)) % 3;
    j -= pad;
    dst[j] = '\0';
}

int jwt_base64url_decode(const char *src, uint8_t *dst, size_t *dst_len) {
    static int8_t decode_table[256];
    static int table_init = 0;
    size_t i, j, len;
    size_t pad;

    if (!table_init) {
        memset(decode_table, -1, sizeof(decode_table));
        for (i = 0; i < 64; i++) decode_table[(int)B64URL[i]] = (int8_t)i;
        table_init = 1;
    }

    len = strlen(src);
    pad = (4 - (len % 4)) % 4;
    for (i = 0, j = 0; i < len; ) {
        int8_t v0 = (i < len) ? decode_table[(int)(uint8_t)src[i++]] : 0;
        int8_t v1 = (i < len) ? decode_table[(int)(uint8_t)src[i++]] : 0;
        int8_t v2 = (i < len) ? decode_table[(int)(uint8_t)src[i++]] : 0;
        int8_t v3 = (i < len) ? decode_table[(int)(uint8_t)src[i++]] : 0;
        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) return -1;
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

int jwt_hs256_sign(const uint8_t *data, size_t data_len,
                    const JwtSigningKey *key,
                    uint8_t *sig_out, size_t *sig_len) {
    unsigned int out_len = 0;
    HMAC(EVP_sha256(), key->secret, (int)key->secret_len,
         data, data_len, sig_out, &out_len);
    *sig_len = out_len;
    return 0;
}

int jwt_hs256_verify(const uint8_t *data, size_t data_len,
                      const JwtSigningKey *key,
                      const uint8_t *sig, size_t sig_len) {
    uint8_t computed[64];
    unsigned int out_len = 0;
    HMAC(EVP_sha256(), key->secret, (int)key->secret_len,
         data, data_len, computed, &out_len);
    if (out_len != sig_len) return 0;
    return (memcmp(computed, sig, sig_len) == 0) ? 1 : 0;
}

int jwt_rs256_sign_sim(const uint8_t *data, size_t data_len,
                        const JwtSigningKey *key,
                        uint8_t *sig_out, size_t *sig_len) {
    /* Simplified RSA256: uses HMAC-SHA256 as simulation */
    unsigned int out_len = 0;
    HMAC(EVP_sha256(), key->secret, (int)key->secret_len,
         data, data_len, sig_out, &out_len);
    *sig_len = out_len;
    return 0;
}

int jwt_rs256_verify_sim(const uint8_t *data, size_t data_len,
                          const JwtSigningKey *key,
                          const uint8_t *sig, size_t sig_len) {
    uint8_t computed[64];
    unsigned int out_len = 0;
    HMAC(EVP_sha256(), key->secret, (int)key->secret_len,
         data, data_len, computed, &out_len);
    if (out_len != sig_len) return 0;
    return (memcmp(computed, sig, sig_len) == 0) ? 1 : 0;
}

const char *jwt_alg_name(JwtAlgorithm alg) {
    switch (alg) {
    case JWT_ALG_NONE: return "none";
    case JWT_ALG_HS256: return "HS256";
    case JWT_ALG_HS384: return "HS384";
    case JWT_ALG_HS512: return "HS512";
    case JWT_ALG_RS256: return "RS256";
    case JWT_ALG_RS384: return "RS384";
    case JWT_ALG_RS512: return "RS512";
    default: return "unknown";
    }
}

void jwt_engine_init(JwtEngine *eng, JwtAlgorithm alg,
                     const uint8_t *secret, size_t secret_len) {
    memset(eng, 0, sizeof(*eng));
    eng->algorithm = alg;
    if (secret && secret_len <= JWT_MAX_SECRET_LEN) {
        memcpy(eng->key.secret, secret, secret_len);
        eng->key.secret_len = secret_len;
    }
    eng->key.algorithm = alg;

    if (alg == JWT_ALG_HS256 || alg == JWT_ALG_HS384 || alg == JWT_ALG_HS512) {
        eng->sign_func = jwt_hs256_sign;
        eng->verify_func = jwt_hs256_verify;
    } else if (alg == JWT_ALG_RS256 || alg == JWT_ALG_RS384 || alg == JWT_ALG_RS512) {
        eng->sign_func = jwt_rs256_sign_sim;
        eng->verify_func = jwt_rs256_verify_sim;
    } else {
        eng->sign_func = NULL;
        eng->verify_func = NULL;
    }
}

void jwt_claims_set_defaults(JwtClaims *claims) {
    memset(claims, 0, sizeof(*claims));
    claims->iat = (int64_t)time(NULL);
    claims->exp = claims->iat + 3600;
    claims->nbf = claims->iat;
}

static int json_escape_str(const char *src, char *dst, size_t dst_size) {
    size_t i = 0, j = 0;
    while (src[i] && j + 2 < dst_size) {
        if (src[i] == '"' || src[i] == '\\') {
            dst[j++] = '\\';
        }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
    return (int)j;
}

static size_t build_claims_json(const JwtClaims *claims, char *buf, size_t buf_size) {
    int len;
    char esc_iss[JWT_MAX_ISS_LEN * 2];
    char esc_sub[JWT_MAX_SUB_LEN * 2];
    char esc_aud[JWT_MAX_AUD_LEN * 2];
    char esc_jti[128];
    char esc_scope[512];

    json_escape_str(claims->iss, esc_iss, sizeof(esc_iss));
    json_escape_str(claims->sub, esc_sub, sizeof(esc_sub));
    json_escape_str(claims->aud, esc_aud, sizeof(esc_aud));
    json_escape_str(claims->jti, esc_jti, sizeof(esc_jti));
    json_escape_str(claims->scope, esc_scope, sizeof(esc_scope));

    len = snprintf(buf, buf_size,
        "{\"iss\":\"%s\",\"sub\":\"%s\",\"aud\":\"%s\","
        "\"exp\":%lld,\"iat\":%lld,\"nbf\":%lld,"
        "\"jti\":\"%s\",\"scope\":\"%s\"}",
        esc_iss, esc_sub, esc_aud,
        (long long)claims->exp, (long long)claims->iat, (long long)claims->nbf,
        esc_jti, esc_scope);
    return (size_t)(len > 0 ? len : 0);
}

int jwt_encode(JwtEngine *eng, const JwtClaims *claims,
               char *token_out, size_t token_size) {
    char header_json[JWT_MAX_HEADER_LEN];
    char payload_json[JWT_MAX_PAYLOAD_LEN];
    char header_b64[JWT_MAX_HEADER_LEN];
    char payload_b64[JWT_MAX_PAYLOAD_LEN];
    char signing_input[JWT_MAX_TOKEN_LEN];
    uint8_t sig[128];
    size_t sig_len = sizeof(sig);
    char sig_b64[256];
    int hdr_len, pl_len;

    hdr_len = snprintf(header_json, sizeof(header_json),
                       "{\"alg\":\"%s\",\"typ\":\"JWT\"}", jwt_alg_name(eng->algorithm));
    if (hdr_len < 0) return -1;

    pl_len = (int)build_claims_json(claims, payload_json, sizeof(payload_json));
    if (pl_len < 0) return -1;

    jwt_base64url_encode((const uint8_t *)header_json, (size_t)hdr_len,
                         header_b64, sizeof(header_b64));
    jwt_base64url_encode((const uint8_t *)payload_json, (size_t)pl_len,
                         payload_b64, sizeof(payload_b64));

    snprintf(signing_input, sizeof(signing_input), "%s.%s", header_b64, payload_b64);

    if (eng->sign_func) {
        eng->sign_func((const uint8_t *)signing_input, strlen(signing_input),
                       &eng->key, sig, &sig_len);
    } else {
        sig_len = 0;
    }

    jwt_base64url_encode(sig, sig_len, sig_b64, sizeof(sig_b64));
    snprintf(token_out, token_size, "%s.%s.%s", header_b64, payload_b64, sig_b64);
    return 0;
}

static int parse_json_int(const char *json, const char *key, int64_t *val) {
    char search[128];
    const char *p;
    snprintf(search, sizeof(search), "\"%s\":", key);
    p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    *val = (int64_t)atoll(p);
    return 0;
}

static int parse_json_str(const char *json, const char *key, char *val, size_t val_size) {
    char search[128];
    const char *p, *start, *end;
    size_t len;
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    start = p;
    end = strchr(start, '"');
    if (!end) return -1;
    len = (size_t)(end - start);
    if (len >= val_size) len = val_size - 1;
    memcpy(val, start, len);
    val[len] = '\0';
    return 0;
}

int jwt_decode(JwtEngine *eng, const char *token,
               JwtClaims *claims_out) {
    char token_copy[JWT_MAX_TOKEN_LEN];
    char *header_b64, *payload_b64, *signature_b64;
    uint8_t header_json[JWT_MAX_HEADER_LEN];
    uint8_t payload_json[JWT_MAX_PAYLOAD_LEN];
    size_t header_len = sizeof(header_json), payload_len = sizeof(payload_json);
    char signing_input[JWT_MAX_TOKEN_LEN];
    uint8_t expected_sig[128];
    size_t expected_sig_len = sizeof(expected_sig);
    char *saveptr;

    if (strlen(token) >= JWT_MAX_TOKEN_LEN) return -1;
    strncpy(token_copy, token, JWT_MAX_TOKEN_LEN - 1);
    token_copy[JWT_MAX_TOKEN_LEN - 1] = '\0';

    header_b64 = strtok_s(token_copy, ".", &saveptr);
    payload_b64 = strtok_s(NULL, ".", &saveptr);
    signature_b64 = strtok_s(NULL, ".", &saveptr);

    if (!header_b64 || !payload_b64) return -2;

    jwt_base64url_decode(header_b64, header_json, &header_len);
    header_json[header_len] = '\0';

    jwt_base64url_decode(payload_b64, payload_json, &payload_len);
    payload_json[payload_len] = '\0';

    snprintf(signing_input, sizeof(signing_input), "%s.%s", header_b64, payload_b64);

    if (eng->verify_func && signature_b64) {
        uint8_t provided_sig[128];
        size_t provided_len = sizeof(provided_sig);
        jwt_base64url_decode(signature_b64, provided_sig, &provided_len);
        if (!eng->verify_func((const uint8_t *)signing_input, strlen(signing_input),
                              &eng->key, provided_sig, provided_len)) {
            return -3;
        }
    }

    memset(claims_out, 0, sizeof(*claims_out));
    parse_json_str((const char *)payload_json, "iss", claims_out->iss, JWT_MAX_ISS_LEN);
    parse_json_str((const char *)payload_json, "sub", claims_out->sub, JWT_MAX_SUB_LEN);
    parse_json_str((const char *)payload_json, "aud", claims_out->aud, JWT_MAX_AUD_LEN);
    parse_json_int((const char *)payload_json, "exp", &claims_out->exp);
    parse_json_int((const char *)payload_json, "iat", &claims_out->iat);
    parse_json_int((const char *)payload_json, "nbf", &claims_out->nbf);
    parse_json_str((const char *)payload_json, "jti", claims_out->jti, 64);
    parse_json_str((const char *)payload_json, "scope", claims_out->scope, 256);

    return 0;
}

int jwt_validate_expiry(const JwtClaims *claims, time_t now) {
    if (now == 0) now = time(NULL);
    if (claims->exp > 0 && (int64_t)now > claims->exp) return 0;
    if (claims->nbf > 0 && (int64_t)now < claims->nbf) return 0;
    return 1;
}

int jwt_validate_issuer(const JwtClaims *claims, const char *expected_iss) {
    if (!expected_iss || expected_iss[0] == '\0') return 1;
    return (strcmp(claims->iss, expected_iss) == 0) ? 1 : 0;
}

int jwt_validate_audience(const JwtClaims *claims, const char *expected_aud) {
    if (!expected_aud || expected_aud[0] == '\0') return 1;
    return (strcmp(claims->aud, expected_aud) == 0) ? 1 : 0;
}

int jwt_validate_all(JwtEngine *eng, const char *token,
                     const char *expected_iss, const char *expected_aud,
                     JwtClaims *claims_out) {
    JwtClaims claims;
    int rc = jwt_decode(eng, token, &claims);
    if (rc != 0) return rc;
    if (!jwt_validate_expiry(&claims, 0)) return -10;
    if (!jwt_validate_issuer(&claims, expected_iss)) return -11;
    if (!jwt_validate_audience(&claims, expected_aud)) return -12;
    if (claims_out) memcpy(claims_out, &claims, sizeof(JwtClaims));
    return 0;
}

int jwt_refresh_token(JwtEngine *eng, const char *token,
                      int64_t new_expiry, char *new_token_out, size_t size) {
    JwtClaims claims;
    int rc = jwt_decode(eng, token, &claims);
    if (rc != 0) return rc;
    claims.exp = new_expiry;
    claims.iat = (int64_t)time(NULL);
    return jwt_encode(eng, &claims, new_token_out, size);
}
