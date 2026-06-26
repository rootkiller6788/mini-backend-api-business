#include "oauth2_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t _oauth2_seed = 0xDEADBEEF;

static uint32_t oauth2_xorshift32(void) {
    _oauth2_seed ^= _oauth2_seed << 13;
    _oauth2_seed ^= _oauth2_seed >> 17;
    _oauth2_seed ^= _oauth2_seed << 5;
    return _oauth2_seed;
}

void oauth2_server_init(OAuth2Server *srv) {
    memset(srv, 0, sizeof(*srv));
}

void oauth2_generate_random_hex(char *buf, size_t len) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i + 1 < len; i++) {
        uint32_t r = oauth2_xorshift32();
        buf[i] = hex[r & 15];
    }
    buf[len - 1] = '\0';
}

int oauth2_register_client(OAuth2Server *srv, const char *client_id,
                            const char *client_secret, const char *redirect_uri,
                            const char *scope, int is_confidential) {
    size_t i;
    if (srv->client_count >= OAUTH2_MAX_CLIENTS) return -1;
    for (i = 0; i < srv->client_count; i++) {
        if (strcmp(srv->clients[i].client_id, client_id) == 0) return -2;
    }
    i = srv->client_count;
    strncpy(srv->clients[i].client_id, client_id, OAUTH2_CLIENT_ID_LEN - 1);
    strncpy(srv->clients[i].client_secret, client_secret, OAUTH2_CLIENT_SECRET_LEN - 1);
    strncpy(srv->clients[i].redirect_uri, redirect_uri, OAUTH2_MAX_REDIRECT_URI - 1);
    strncpy(srv->clients[i].scope, scope, OAUTH2_SCOPE_LEN - 1);
    srv->clients[i].is_confidential = is_confidential;
    srv->clients[i].created_at = time(NULL);
    srv->client_count++;
    return 0;
}

int oauth2_validate_client(const OAuth2Server *srv, const char *client_id,
                            const char *client_secret) {
    size_t i;
    for (i = 0; i < srv->client_count; i++) {
        if (strcmp(srv->clients[i].client_id, client_id) == 0) {
            if (srv->clients[i].is_confidential) {
                return (strcmp(srv->clients[i].client_secret, client_secret) == 0) ? 1 : 0;
            }
            return 1;
        }
    }
    return 0;
}

int oauth2_create_auth_code(OAuth2Server *srv, const char *client_id,
                             const char *redirect_uri, const char *username,
                             const char *scope, char *code_out, size_t code_size) {
    size_t i;
    if (srv->auth_code_count >= OAUTH2_MAX_TOKENS) return -1;
    if (!oauth2_validate_client(srv, client_id, "")) return -2;

    i = srv->auth_code_count;
    oauth2_generate_random_hex(srv->auth_codes[i].code, OAUTH2_AUTH_CODE_LEN);
    strncpy(srv->auth_codes[i].client_id, client_id, OAUTH2_CLIENT_ID_LEN - 1);
    strncpy(srv->auth_codes[i].redirect_uri, redirect_uri, OAUTH2_MAX_REDIRECT_URI - 1);
    strncpy(srv->auth_codes[i].username, username, OAUTH2_USERNAME_LEN - 1);
    strncpy(srv->auth_codes[i].scope, scope, OAUTH2_SCOPE_LEN - 1);
    srv->auth_codes[i].expires_at = time(NULL) + OAUTH2_AUTH_CODE_TTL;
    srv->auth_codes[i].used = 0;
    srv->auth_code_count++;

    if (code_out && code_size > 0) {
        strncpy(code_out, srv->auth_codes[i].code, code_size - 1);
        code_out[code_size - 1] = '\0';
    }
    return 0;
}

static OAuth2AuthCode *oauth2_find_auth_code(OAuth2Server *srv, const char *code) {
    size_t i;
    for (i = 0; i < srv->auth_code_count; i++) {
        if (strcmp(srv->auth_codes[i].code, code) == 0) return &srv->auth_codes[i];
    }
    return NULL;
}

static OAuth2TokenEntry *oauth2_find_access_token(OAuth2Server *srv,
                                                   const char *access_token) {
    size_t i;
    for (i = 0; i < srv->token_count; i++) {
        if (strcmp(srv->tokens[i].access_token, access_token) == 0) return &srv->tokens[i];
    }
    return NULL;
}

static OAuth2TokenEntry *oauth2_find_refresh_token(OAuth2Server *srv,
                                                    const char *refresh_token) {
    size_t i;
    for (i = 0; i < srv->token_count; i++) {
        if (strcmp(srv->tokens[i].refresh_token, refresh_token) == 0) return &srv->tokens[i];
    }
    return NULL;
}

static int oauth2_store_token(OAuth2Server *srv, const char *client_id,
                               const char *username, const char *scope,
                               char *at_out, size_t at_size,
                               char *rt_out, size_t rt_size) {
    size_t i;
    if (srv->token_count >= OAUTH2_MAX_TOKENS) return -1;

    i = srv->token_count;
    oauth2_generate_random_hex(srv->tokens[i].access_token, OAUTH2_ACCESS_TOKEN_LEN);
    oauth2_generate_random_hex(srv->tokens[i].refresh_token, OAUTH2_REFRESH_TOKEN_LEN);
    strncpy(srv->tokens[i].client_id, client_id, OAUTH2_CLIENT_ID_LEN - 1);
    if (username) strncpy(srv->tokens[i].username, username, OAUTH2_USERNAME_LEN - 1);
    if (scope) strncpy(srv->tokens[i].scope, scope, OAUTH2_SCOPE_LEN - 1);
    srv->tokens[i].token_type = OAUTH2_TOKEN_BEARER;
    srv->tokens[i].access_expires_at = time(NULL) + OAUTH2_ACCESS_TOKEN_TTL;
    srv->tokens[i].refresh_expires_at = time(NULL) + OAUTH2_REFRESH_TOKEN_TTL;
    srv->tokens[i].revoked = 0;
    srv->token_count++;

    if (at_out && at_size > 0) {
        strncpy(at_out, srv->tokens[i].access_token, at_size - 1);
        at_out[at_size - 1] = '\0';
    }
    if (rt_out && rt_size > 0) {
        strncpy(rt_out, srv->tokens[i].refresh_token, rt_size - 1);
        rt_out[rt_size - 1] = '\0';
    }
    return 0;
}

int oauth2_exchange_code_for_token(OAuth2Server *srv, const char *code,
                                    const char *client_id, const char *client_secret,
                                    const char *redirect_uri,
                                    char *access_token_out, size_t at_size,
                                    char *refresh_token_out, size_t rt_size) {
    OAuth2AuthCode *ac;
    if (!oauth2_validate_client(srv, client_id, client_secret)) return -1;

    ac = oauth2_find_auth_code(srv, code);
    if (!ac) return -2;
    if (ac->used) return -3;
    if (time(NULL) > ac->expires_at) return -4;
    if (strcmp(ac->client_id, client_id) != 0) return -5;
    if (redirect_uri && strcmp(ac->redirect_uri, redirect_uri) != 0) return -6;

    ac->used = 1;
    return oauth2_store_token(srv, client_id, ac->username, ac->scope,
                              access_token_out, at_size,
                              refresh_token_out, rt_size);
}

int oauth2_client_credentials_grant(OAuth2Server *srv, const char *client_id,
                                     const char *client_secret, const char *scope,
                                     char *access_token_out, size_t at_size) {
    if (!oauth2_validate_client(srv, client_id, client_secret)) return -1;
    return oauth2_store_token(srv, client_id, NULL, scope,
                              access_token_out, at_size, NULL, 0);
}

int oauth2_refresh_token(OAuth2Server *srv, const char *refresh_token,
                          const char *client_id, const char *client_secret,
                          char *new_access_token_out, size_t at_size,
                          char *new_refresh_token_out, size_t rt_size) {
    OAuth2TokenEntry *entry;
    if (!oauth2_validate_client(srv, client_id, client_secret)) return -1;

    entry = oauth2_find_refresh_token(srv, refresh_token);
    if (!entry) return -2;
    if (entry->revoked) return -3;
    if (time(NULL) > entry->refresh_expires_at) return -4;
    if (strcmp(entry->client_id, client_id) != 0) return -5;

    entry->access_expires_at = time(NULL) + OAUTH2_ACCESS_TOKEN_TTL;
    oauth2_generate_random_hex(entry->access_token, OAUTH2_ACCESS_TOKEN_LEN);
    if (new_refresh_token_out) {
        oauth2_generate_random_hex(entry->refresh_token, OAUTH2_REFRESH_TOKEN_LEN);
        entry->refresh_expires_at = time(NULL) + OAUTH2_REFRESH_TOKEN_TTL;
    }

    if (new_access_token_out && at_size > 0) {
        strncpy(new_access_token_out, entry->access_token, at_size - 1);
        new_access_token_out[at_size - 1] = '\0';
    }
    if (new_refresh_token_out && rt_size > 0) {
        strncpy(new_refresh_token_out, entry->refresh_token, rt_size - 1);
        new_refresh_token_out[rt_size - 1] = '\0';
    }
    return 0;
}

int oauth2_validate_access_token(const OAuth2Server *srv, const char *access_token,
                                  char *client_id_out, size_t ci_size,
                                  char *scope_out, size_t sc_size) {
    OAuth2TokenEntry *entry = oauth2_find_access_token((OAuth2Server *)srv, access_token);
    if (!entry) return -1;
    if (entry->revoked) return -2;
    if (time(NULL) > entry->access_expires_at) return -3;

    if (client_id_out && ci_size > 0) {
        strncpy(client_id_out, entry->client_id, ci_size - 1);
        client_id_out[ci_size - 1] = '\0';
    }
    if (scope_out && sc_size > 0) {
        strncpy(scope_out, entry->scope, sc_size - 1);
        scope_out[sc_size - 1] = '\0';
    }
    return 0;
}

int oauth2_revoke_token(OAuth2Server *srv, const char *access_token) {
    OAuth2TokenEntry *entry = oauth2_find_access_token(srv, access_token);
    if (!entry) return -1;
    entry->revoked = 1;
    return 0;
}

void oauth2_dump_state(const OAuth2Server *srv) {
    size_t i;
    printf("=== OAuth2 Server State ===\n");
    printf("Clients: %zu\n", srv->client_count);
    for (i = 0; i < srv->client_count; i++) {
        printf("  [%zu] %s (confidential=%d)\n", i,
               srv->clients[i].client_id, srv->clients[i].is_confidential);
    }
    printf("Auth Codes: %zu\n", srv->auth_code_count);
    for (i = 0; i < srv->auth_code_count; i++) {
        printf("  [%zu] %s user=%s used=%d expires=%ld\n", i,
               srv->auth_codes[i].code, srv->auth_codes[i].username,
               srv->auth_codes[i].used, (long)srv->auth_codes[i].expires_at);
    }
    printf("Tokens: %zu\n", srv->token_count);
    for (i = 0; i < srv->token_count; i++) {
        printf("  [%zu] access=%s refresh=%s revoked=%d\n", i,
               srv->tokens[i].access_token, srv->tokens[i].refresh_token,
               srv->tokens[i].revoked);
    }
}
