#include "oauth2_server.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    OAuth2Server server;
    char auth_code[OAUTH2_AUTH_CODE_LEN];
    char access_token[OAUTH2_ACCESS_TOKEN_LEN];
    char refresh_token[OAUTH2_REFRESH_TOKEN_LEN];
    char new_at[OAUTH2_ACCESS_TOKEN_LEN];
    char new_rt[OAUTH2_REFRESH_TOKEN_LEN];
    char client_id_out[OAUTH2_CLIENT_ID_LEN];
    char scope_out[OAUTH2_SCOPE_LEN];
    int rc;

    oauth2_server_init(&server);

    printf("=== OAuth2 Server Demo ===\n\n");

    rc = oauth2_register_client(&server,
        "webapp-client", "secret123",
        "https://app.example.com/callback",
        "read write", 1);
    printf("[1] Register client: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = oauth2_register_client(&server,
        "mobile-client", "mobile-secret",
        "myapp://callback",
        "read", 0);
    printf("[2] Register mobile client: %s\n", rc == 0 ? "OK" : "FAIL");

    printf("\n--- Authorization Code Flow ---\n");

    rc = oauth2_create_auth_code(&server,
        "webapp-client",
        "https://app.example.com/callback",
        "alice", "read write",
        auth_code, sizeof(auth_code));
    printf("[3] Create auth code: %s (code=%s)\n",
           rc == 0 ? "OK" : "FAIL", auth_code);

    rc = oauth2_exchange_code_for_token(&server,
        auth_code,
        "webapp-client", "secret123",
        "https://app.example.com/callback",
        access_token, sizeof(access_token),
        refresh_token, sizeof(refresh_token));
    printf("[4] Exchange code for token: %s\n", rc == 0 ? "OK" : "FAIL");
    printf("    Access:  %s\n", access_token);
    printf("    Refresh: %s\n", refresh_token);

    rc = oauth2_validate_access_token(&server,
        access_token,
        client_id_out, sizeof(client_id_out),
        scope_out, sizeof(scope_out));
    printf("[5] Validate access token: %s\n", rc == 0 ? "VALID" : "INVALID");
    printf("    Client: %s, Scope: %s\n", client_id_out, scope_out);

    printf("\n--- Refresh Token Flow ---\n");

    rc = oauth2_refresh_token(&server,
        refresh_token,
        "webapp-client", "secret123",
        new_at, sizeof(new_at),
        new_rt, sizeof(new_rt));
    printf("[6] Refresh token: %s\n", rc == 0 ? "OK" : "FAIL");
    printf("    New access:  %s\n", new_at);
    printf("    New refresh: %s\n", new_rt);

    printf("\n--- Client Credentials Flow ---\n");

    rc = oauth2_client_credentials_grant(&server,
        "mobile-client", "mobile-secret",
        "read",
        access_token, sizeof(access_token));
    printf("[7] Client credentials grant: %s\n", rc == 0 ? "OK" : "FAIL");
    printf("    Access: %s\n", access_token);

    rc = oauth2_revoke_token(&server, access_token);
    printf("[8] Revoke token: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = oauth2_validate_access_token(&server,
        access_token,
        client_id_out, sizeof(client_id_out),
        scope_out, sizeof(scope_out));
    printf("[9] Validate revoked token: %s\n", rc == 0 ? "VALID" : "EXPECTED_INVALID");

    printf("\n");
    oauth2_dump_state(&server);

    printf("\n=== All OAuth2 tests completed ===\n");
    return 0;
}
