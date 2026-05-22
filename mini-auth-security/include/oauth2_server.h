#ifndef OAUTH2_SERVER_H
#define OAUTH2_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define OAUTH2_MAX_CLIENTS      256
#define OAUTH2_MAX_TOKENS       1024
#define OAUTH2_MAX_REDIRECT_URI 512
#define OAUTH2_CLIENT_ID_LEN    64
#define OAUTH2_CLIENT_SECRET_LEN 128
#define OAUTH2_AUTH_CODE_LEN    64
#define OAUTH2_ACCESS_TOKEN_LEN 128
#define OAUTH2_REFRESH_TOKEN_LEN 128
#define OAUTH2_STATE_LEN        64
#define OAUTH2_SCOPE_LEN        256
#define OAUTH2_USERNAME_LEN     128

#define OAUTH2_ACCESS_TOKEN_TTL  3600
#define OAUTH2_REFRESH_TOKEN_TTL 86400
#define OAUTH2_AUTH_CODE_TTL     600

typedef enum {
    OAUTH2_GRANT_AUTH_CODE = 0,
    OAUTH2_GRANT_CLIENT_CREDENTIALS,
    OAUTH2_GRANT_REFRESH_TOKEN
} OAuth2GrantType;

typedef enum {
    OAUTH2_TOKEN_BEARER = 0,
    OAUTH2_TOKEN_MAC
} OAuth2TokenType;

typedef struct {
    char client_id[OAUTH2_CLIENT_ID_LEN];
    char client_secret[OAUTH2_CLIENT_SECRET_LEN];
    char redirect_uri[OAUTH2_MAX_REDIRECT_URI];
    char scope[OAUTH2_SCOPE_LEN];
    int  is_confidential;
    time_t created_at;
} OAuth2Client;

typedef struct {
    char code[OAUTH2_AUTH_CODE_LEN];
    char client_id[OAUTH2_CLIENT_ID_LEN];
    char redirect_uri[OAUTH2_MAX_REDIRECT_URI];
    char username[OAUTH2_USERNAME_LEN];
    char scope[OAUTH2_SCOPE_LEN];
    time_t expires_at;
    int  used;
} OAuth2AuthCode;

typedef struct {
    char access_token[OAUTH2_ACCESS_TOKEN_LEN];
    char refresh_token[OAUTH2_REFRESH_TOKEN_LEN];
    char client_id[OAUTH2_CLIENT_ID_LEN];
    char username[OAUTH2_USERNAME_LEN];
    char scope[OAUTH2_SCOPE_LEN];
    OAuth2TokenType token_type;
    time_t access_expires_at;
    time_t refresh_expires_at;
    int  revoked;
} OAuth2TokenEntry;

typedef struct {
    OAuth2Client    clients[OAUTH2_MAX_CLIENTS];
    OAuth2AuthCode  auth_codes[OAUTH2_MAX_TOKENS];
    OAuth2TokenEntry tokens[OAUTH2_MAX_TOKENS];
    size_t client_count;
    size_t auth_code_count;
    size_t token_count;
} OAuth2Server;

void   oauth2_server_init(OAuth2Server *srv);
int    oauth2_register_client(OAuth2Server *srv, const char *client_id,
                              const char *client_secret, const char *redirect_uri,
                              const char *scope, int is_confidential);
int    oauth2_validate_client(const OAuth2Server *srv, const char *client_id,
                              const char *client_secret);

int    oauth2_create_auth_code(OAuth2Server *srv, const char *client_id,
                               const char *redirect_uri, const char *username,
                               const char *scope, char *code_out, size_t code_size);
int    oauth2_exchange_code_for_token(OAuth2Server *srv, const char *code,
                                      const char *client_id, const char *client_secret,
                                      const char *redirect_uri,
                                      char *access_token_out, size_t at_size,
                                      char *refresh_token_out, size_t rt_size);

int    oauth2_client_credentials_grant(OAuth2Server *srv, const char *client_id,
                                       const char *client_secret, const char *scope,
                                       char *access_token_out, size_t at_size);

int    oauth2_refresh_token(OAuth2Server *srv, const char *refresh_token,
                            const char *client_id, const char *client_secret,
                            char *new_access_token_out, size_t at_size,
                            char *new_refresh_token_out, size_t rt_size);

int    oauth2_validate_access_token(const OAuth2Server *srv, const char *access_token,
                                    char *client_id_out, size_t ci_size,
                                    char *scope_out, size_t sc_size);

int    oauth2_revoke_token(OAuth2Server *srv, const char *access_token);

void   oauth2_generate_random_hex(char *buf, size_t len);
void   oauth2_dump_state(const OAuth2Server *srv);

#endif
