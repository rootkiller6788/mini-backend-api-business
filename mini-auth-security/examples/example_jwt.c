#include "jwt_auth.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    JwtEngine eng;
    JwtClaims claims;
    JwtClaims decoded;
    char token[JWT_MAX_TOKEN_LEN];
    char refreshed[JWT_MAX_TOKEN_LEN];
    uint8_t secret[] = "my-super-secret-key-for-hs256-min-32-chars!";
    int rc;

    printf("=== JWT Auth Demo ===\n\n");

    printf("--- HS256 Symmetric Signing ---\n");
    jwt_engine_init(&eng, JWT_ALG_HS256, secret, strlen((const char *)secret));

    jwt_claims_set_defaults(&claims);
    strncpy(claims.iss, "https://auth.example.com", JWT_MAX_ISS_LEN - 1);
    strncpy(claims.sub, "user-42", JWT_MAX_SUB_LEN - 1);
    strncpy(claims.aud, "api.example.com", JWT_MAX_AUD_LEN - 1);
    strncpy(claims.scope, "read write", sizeof(claims.scope) - 1);
    claims.exp = (int64_t)time(NULL) + 3600;

    rc = jwt_encode(&eng, &claims, token, sizeof(token));
    printf("[1] Encode JWT (HS256): %s\n", rc == 0 ? "OK" : "FAIL");
    printf("    Token: %s\n", token);

    rc = jwt_decode(&eng, token, &decoded);
    printf("[2] Decode JWT: %s\n", rc == 0 ? "OK" : "FAIL");
    printf("    iss=%s sub=%s aud=%s\n", decoded.iss, decoded.sub, decoded.aud);
    printf("    exp=%lld iat=%lld\n",
           (long long)decoded.exp, (long long)decoded.iat);

    printf("\n--- Token Validation ---\n");

    rc = jwt_validate_expiry(&decoded, 0);
    printf("[3] Validate expiry: %s\n", rc == 1 ? "VALID" : "EXPIRED");

    rc = jwt_validate_issuer(&decoded, "https://auth.example.com");
    printf("[4] Validate issuer: %s\n", rc == 1 ? "MATCH" : "MISMATCH");

    rc = jwt_validate_audience(&decoded, "api.example.com");
    printf("[5] Validate audience: %s\n", rc == 1 ? "MATCH" : "MISMATCH");

    rc = jwt_validate_all(&eng, token,
                          "https://auth.example.com", "api.example.com", NULL);
    printf("[6] Validate all: %s\n", rc == 0 ? "VALID" : "FAIL");

    printf("\n--- Token Refresh ---\n");

    rc = jwt_refresh_token(&eng, token,
                           (int64_t)time(NULL) + 7200,
                           refreshed, sizeof(refreshed));
    printf("[7] Refresh token: %s\n", rc == 0 ? "OK" : "FAIL");
    printf("    New token: %s\n", refreshed);

    rc = jwt_decode(&eng, refreshed, &decoded);
    printf("[8] Decode refreshed: %s\n", rc == 0 ? "OK" : "FAIL");
    printf("    New exp=%lld iat=%lld\n",
           (long long)decoded.exp, (long long)decoded.iat);

    printf("\n--- RS256 Asymmetric (simulated) ---\n");

    jwt_engine_init(&eng, JWT_ALG_RS256, secret, strlen((const char *)secret));
    jwt_claims_set_defaults(&claims);
    strncpy(claims.iss, "https://idp.example.com", JWT_MAX_ISS_LEN - 1);
    strncpy(claims.sub, "admin", JWT_MAX_SUB_LEN - 1);

    rc = jwt_encode(&eng, &claims, token, sizeof(token));
    printf("[9] Encode JWT (RS256 sim): %s\n", rc == 0 ? "OK" : "FAIL");
    printf("    Token: %s\n", token);

    rc = jwt_decode(&eng, token, &decoded);
    printf("[10] Decode RS256 JWT: %s\n", rc == 0 ? "OK" : "FAIL");
    printf("    iss=%s sub=%s\n", decoded.iss, decoded.sub);

    printf("\n=== All JWT tests completed ===\n");
    return 0;
}
