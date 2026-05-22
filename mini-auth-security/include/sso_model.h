#ifndef SSO_MODEL_H
#define SSO_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define SSO_MAX_SERVICE_PROVIDERS 64
#define SSO_MAX_SESSIONS          1024
#define SSO_MAX_TICKETS           2048
#define SSO_MAX_USERS             512
#define SSO_MAX_URL_LEN           512
#define SSO_MAX_ENTITY_ID_LEN     256
#define SSO_MAX_CERT_LEN          2048
#define SSO_MAX_SAML_ASSERTION_LEN 8192
#define SSO_MAX_TICKET_LEN        128
#define SSO_MAX_SESSION_ID_LEN    128
#define SSO_MAX_USERNAME_LEN      128

typedef enum {
    SSO_PROTOCOL_SAML2 = 0,
    SSO_PROTOCOL_OIDC,
    SSO_PROTOCOL_CAS
} SsoProtocol;

typedef enum {
    SSO_BINDING_REDIRECT = 0,
    SSO_BINDING_POST,
    SSO_BINDING_ARTIFACT
} SsoBinding;

typedef struct {
    char entity_id[SSO_MAX_ENTITY_ID_LEN];
    char acs_url[SSO_MAX_URL_LEN];
    char slo_url[SSO_MAX_URL_LEN];
    char cert_pem[SSO_MAX_CERT_LEN];
    SsoProtocol protocol;
    SsoBinding preferred_binding;
    int  want_authn_signed;
    int  want_assertion_signed;
    int  trusted;
} SsoServiceProvider;

typedef struct {
    char      entity_id[SSO_MAX_ENTITY_ID_LEN];
    char      sso_url[SSO_MAX_URL_LEN];
    char      slo_url[SSO_MAX_URL_LEN];
    char      cert_pem[SSO_MAX_CERT_LEN];
    char      private_key_pem[SSO_MAX_CERT_LEN];
    SsoBinding preferred_binding;
} SsoIdentityProvider;

typedef struct {
    char ticket[SSO_MAX_TICKET_LEN];
    char username[SSO_MAX_USERNAME_LEN];
    char session_id[SSO_MAX_SESSION_ID_LEN];
    char sp_entity_id[SSO_MAX_ENTITY_ID_LEN];
    char assertion_xml[SSO_MAX_SAML_ASSERTION_LEN];
    time_t created_at;
    time_t expires_at;
    int consumed;
    int valid;
} SsoTicket;

typedef struct {
    char session_id[SSO_MAX_SESSION_ID_LEN];
    char username[SSO_MAX_USERNAME_LEN];
    char idp_entity_id[SSO_MAX_ENTITY_ID_LEN];
    time_t created_at;
    time_t expires_at;
    time_t last_accessed_at;
    int valid;
} SsoSession;

typedef struct {
    char username[SSO_MAX_USERNAME_LEN];
    char email[SSO_MAX_USERNAME_LEN];
    char display_name[SSO_MAX_USERNAME_LEN];
    time_t created_at;
} SsoUser;

typedef struct {
    SsoIdentityProvider    idp;
    SsoServiceProvider     sps[SSO_MAX_SERVICE_PROVIDERS];
    SsoTicket              tickets[SSO_MAX_TICKETS];
    SsoSession             sessions[SSO_MAX_SESSIONS];
    SsoUser                users[SSO_MAX_USERS];
    size_t sp_count;
    size_t ticket_count;
    size_t session_count;
    size_t user_count;
} SsoModel;

void sso_model_init(SsoModel *model);

void sso_idp_configure(SsoModel *model, const char *entity_id,
                       const char *sso_url, const char *slo_url,
                       const char *cert_pem, SsoBinding binding);

int  sso_register_sp(SsoModel *model, const char *entity_id,
                     const char *acs_url, const char *slo_url,
                     const char *cert_pem, SsoProtocol protocol,
                     SsoBinding binding);

int  sso_establish_trust(SsoModel *model, const char *sp_entity_id);

int  sso_create_user(SsoModel *model, const char *username,
                     const char *email, const char *display_name);

int  sso_authenticate_user(SsoModel *model, const char *username,
                           const char *sp_entity_id,
                           char *session_id_out, size_t sid_size,
                           char *saml_assertion_out, size_t as_size);

int  sso_create_ticket(SsoModel *model, const char *username,
                       const char *sp_entity_id,
                       const char *session_id,
                       char *ticket_out, size_t t_size);

int  sso_validate_ticket(SsoModel *model, const char *ticket,
                         const char *sp_entity_id,
                         char *username_out, size_t u_size,
                         char *saml_assertion_out, size_t as_size);

int  sso_validate_session(SsoModel *model, const char *session_id,
                          char *username_out, size_t u_size);

int  sso_logout(SsoModel *model, const char *session_id);

int  sso_generate_saml_assertion(const SsoModel *model,
                                 const char *username,
                                 const char *sp_entity_id,
                                 char *assertion_out, size_t as_size);

void sso_dump_state(const SsoModel *model);

#endif
