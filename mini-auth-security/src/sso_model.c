#include "sso_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t _sso_seed = 0xBEEFCAFE;

static uint32_t sso_xorshift32(void) {
    _sso_seed ^= _sso_seed << 13;
    _sso_seed ^= _sso_seed >> 17;
    _sso_seed ^= _sso_seed << 5;
    return _sso_seed;
}

static void sso_generate_id(char *buf, size_t len) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i + 1 < len; i++) {
        buf[i] = hex[sso_xorshift32() & 15];
    }
    buf[len - 1] = '\0';
}

void sso_model_init(SsoModel *model) {
    memset(model, 0, sizeof(*model));
}

void sso_idp_configure(SsoModel *model, const char *entity_id,
                       const char *sso_url, const char *slo_url,
                       const char *cert_pem, SsoBinding binding) {
    strncpy(model->idp.entity_id, entity_id, SSO_MAX_ENTITY_ID_LEN - 1);
    strncpy(model->idp.sso_url, sso_url, SSO_MAX_URL_LEN - 1);
    strncpy(model->idp.slo_url, slo_url, SSO_MAX_URL_LEN - 1);
    strncpy(model->idp.cert_pem, cert_pem, SSO_MAX_CERT_LEN - 1);
    model->idp.preferred_binding = binding;
}

int sso_register_sp(SsoModel *model, const char *entity_id,
                     const char *acs_url, const char *slo_url,
                     const char *cert_pem, SsoProtocol protocol,
                     SsoBinding binding) {
    size_t i;
    if (model->sp_count >= SSO_MAX_SERVICE_PROVIDERS) return -1;
    for (i = 0; i < model->sp_count; i++) {
        if (strcmp(model->sps[i].entity_id, entity_id) == 0) return -2;
    }
    i = model->sp_count;
    strncpy(model->sps[i].entity_id, entity_id, SSO_MAX_ENTITY_ID_LEN - 1);
    strncpy(model->sps[i].acs_url, acs_url, SSO_MAX_URL_LEN - 1);
    strncpy(model->sps[i].slo_url, slo_url, SSO_MAX_URL_LEN - 1);
    strncpy(model->sps[i].cert_pem, cert_pem, SSO_MAX_CERT_LEN - 1);
    model->sps[i].protocol = protocol;
    model->sps[i].preferred_binding = binding;
    model->sps[i].trusted = 0;
    model->sps[i].want_authn_signed = 1;
    model->sps[i].want_assertion_signed = 1;
    model->sp_count++;
    return 0;
}

int sso_establish_trust(SsoModel *model, const char *sp_entity_id) {
    size_t i;
    for (i = 0; i < model->sp_count; i++) {
        if (strcmp(model->sps[i].entity_id, sp_entity_id) == 0) {
            model->sps[i].trusted = 1;
            return 0;
        }
    }
    return -1;
}

int sso_create_user(SsoModel *model, const char *username,
                     const char *email, const char *display_name) {
    size_t i;
    if (model->user_count >= SSO_MAX_USERS) return -1;
    for (i = 0; i < model->user_count; i++) {
        if (strcmp(model->users[i].username, username) == 0) return -2;
    }
    i = model->user_count;
    strncpy(model->users[i].username, username, SSO_MAX_USERNAME_LEN - 1);
    strncpy(model->users[i].email, email, SSO_MAX_USERNAME_LEN - 1);
    strncpy(model->users[i].display_name, display_name, SSO_MAX_USERNAME_LEN - 1);
    model->users[i].created_at = time(NULL);
    model->user_count++;
    return 0;
}

static SsoUser *sso_find_user(const SsoModel *model, const char *username) {
    size_t i;
    for (i = 0; i < model->user_count; i++) {
        if (strcmp(model->users[i].username, username) == 0) return &model->users[i];
    }
    return NULL;
}

static int sso_is_sp_trusted(const SsoModel *model, const char *sp_entity_id) {
    size_t i;
    for (i = 0; i < model->sp_count; i++) {
        if (strcmp(model->sps[i].entity_id, sp_entity_id) == 0) {
            return model->sps[i].trusted;
        }
    }
    return 0;
}

int sso_authenticate_user(SsoModel *model, const char *username,
                           const char *sp_entity_id,
                           char *session_id_out, size_t sid_size,
                           char *saml_assertion_out, size_t as_size) {
    SsoUser *user;
    size_t i;

    if (!sso_is_sp_trusted(model, sp_entity_id)) return -1;
    user = sso_find_user(model, username);
    if (!user) return -2;
    if (model->session_count >= SSO_MAX_SESSIONS) return -3;

    i = model->session_count;
    sso_generate_id(model->sessions[i].session_id, SSO_MAX_SESSION_ID_LEN);
    strncpy(model->sessions[i].username, username, SSO_MAX_USERNAME_LEN - 1);
    strncpy(model->sessions[i].idp_entity_id, model->idp.entity_id, SSO_MAX_ENTITY_ID_LEN - 1);
    model->sessions[i].created_at = time(NULL);
    model->sessions[i].expires_at = time(NULL) + 3600;
    model->sessions[i].last_accessed_at = time(NULL);
    model->sessions[i].valid = 1;
    model->session_count++;

    if (session_id_out && sid_size > 0) {
        strncpy(session_id_out, model->sessions[i].session_id, sid_size - 1);
        session_id_out[sid_size - 1] = '\0';
    }

    sso_generate_saml_assertion(model, username, sp_entity_id,
                                saml_assertion_out, as_size);
    return 0;
}

int sso_create_ticket(SsoModel *model, const char *username,
                       const char *sp_entity_id,
                       const char *session_id,
                       char *ticket_out, size_t t_size) {
    size_t i;

    if (!sso_is_sp_trusted(model, sp_entity_id)) return -1;
    if (model->ticket_count >= SSO_MAX_TICKETS) return -2;

    i = model->ticket_count;
    sso_generate_id(model->tickets[i].ticket, SSO_MAX_TICKET_LEN);
    strncpy(model->tickets[i].username, username, SSO_MAX_USERNAME_LEN - 1);
    strncpy(model->tickets[i].session_id, session_id, SSO_MAX_SESSION_ID_LEN - 1);
    strncpy(model->tickets[i].sp_entity_id, sp_entity_id, SSO_MAX_ENTITY_ID_LEN - 1);
    model->tickets[i].created_at = time(NULL);
    model->tickets[i].expires_at = time(NULL) + 300;
    model->tickets[i].consumed = 0;
    model->tickets[i].valid = 1;
    model->ticket_count++;

    sso_generate_saml_assertion(model, username, sp_entity_id,
                                model->tickets[i].assertion_xml,
                                SSO_MAX_SAML_ASSERTION_LEN);

    if (ticket_out && t_size > 0) {
        strncpy(ticket_out, model->tickets[i].ticket, t_size - 1);
        ticket_out[t_size - 1] = '\0';
    }
    return 0;
}

int sso_validate_ticket(SsoModel *model, const char *ticket,
                         const char *sp_entity_id,
                         char *username_out, size_t u_size,
                         char *saml_assertion_out, size_t as_size) {
    size_t i;
    for (i = 0; i < model->ticket_count; i++) {
        if (strcmp(model->tickets[i].ticket, ticket) == 0) {
            if (!model->tickets[i].valid) return -1;
            if (model->tickets[i].consumed) return -2;
            if (time(NULL) > model->tickets[i].expires_at) return -3;
            if (sp_entity_id &&
                strcmp(model->tickets[i].sp_entity_id, sp_entity_id) != 0) return -4;

            model->tickets[i].consumed = 1;

            if (username_out && u_size > 0) {
                strncpy(username_out, model->tickets[i].username, u_size - 1);
                username_out[u_size - 1] = '\0';
            }
            if (saml_assertion_out && as_size > 0) {
                strncpy(saml_assertion_out, model->tickets[i].assertion_xml, as_size - 1);
                saml_assertion_out[as_size - 1] = '\0';
            }
            return 0;
        }
    }
    return -5;
}

int sso_validate_session(SsoModel *model, const char *session_id,
                          char *username_out, size_t u_size) {
    size_t i;
    for (i = 0; i < model->session_count; i++) {
        if (strcmp(model->sessions[i].session_id, session_id) == 0) {
            if (!model->sessions[i].valid) return -1;
            if (time(NULL) > model->sessions[i].expires_at) {
                model->sessions[i].valid = 0;
                return -2;
            }
            model->sessions[i].last_accessed_at = time(NULL);

            if (username_out && u_size > 0) {
                strncpy(username_out, model->sessions[i].username, u_size - 1);
                username_out[u_size - 1] = '\0';
            }
            return 0;
        }
    }
    return -3;
}

int sso_logout(SsoModel *model, const char *session_id) {
    size_t i;
    for (i = 0; i < model->session_count; i++) {
        if (strcmp(model->sessions[i].session_id, session_id) == 0) {
            model->sessions[i].valid = 0;
            return 0;
        }
    }
    return -1;
}

int sso_generate_saml_assertion(const SsoModel *model,
                                 const char *username,
                                 const char *sp_entity_id,
                                 char *assertion_out, size_t as_size) {
    char now_str[64];
    char expires_str[64];
    time_t now = time(NULL);
    time_t expires = now + 300;

    snprintf(now_str, sizeof(now_str), "%lld", (long long)now);
    snprintf(expires_str, sizeof(expires_str), "%lld", (long long)expires);

    snprintf(assertion_out, as_size,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<saml:Assertion xmlns:saml=\"urn:oasis:names:tc:SAML:2.0:assertion\" "
        "ID=\"_%08x\" IssueInstant=\"%s\" Version=\"2.0\">\n"
        "  <saml:Issuer>%s</saml:Issuer>\n"
        "  <saml:Subject>\n"
        "    <saml:NameID>%s</saml:NameID>\n"
        "  </saml:Subject>\n"
        "  <saml:Conditions NotBefore=\"%s\" NotOnOrAfter=\"%s\">\n"
        "    <saml:AudienceRestriction>\n"
        "      <saml:Audience>%s</saml:Audience>\n"
        "    </saml:AudienceRestriction>\n"
        "  </saml:Conditions>\n"
        "  <saml:AuthnStatement AuthnInstant=\"%s\">\n"
        "    <saml:AuthnContext>\n"
        "      <saml:AuthnContextClassRef>urn:oasis:names:tc:SAML:2.0:ac:classes:Password</saml:AuthnContextClassRef>\n"
        "    </saml:AuthnContext>\n"
        "  </saml:AuthnStatement>\n"
        "</saml:Assertion>",
        sso_xorshift32(), now_str,
        model->idp.entity_id,
        username,
        now_str, expires_str,
        sp_entity_id,
        now_str);
    return 0;
}

void sso_dump_state(const SsoModel *model) {
    size_t i;
    printf("=== SSO Model State ===\n");
    printf("IDP: %s\n", model->idp.entity_id);
    printf("Service Providers: %zu\n", model->sp_count);
    for (i = 0; i < model->sp_count; i++) {
        printf("  [%zu] %s trusted=%d\n", i,
               model->sps[i].entity_id, model->sps[i].trusted);
    }
    printf("Users: %zu\n", model->user_count);
    for (i = 0; i < model->user_count; i++) {
        printf("  [%zu] %s <%s>\n", i,
               model->users[i].username, model->users[i].email);
    }
    printf("Sessions: %zu\n", model->session_count);
    for (i = 0; i < model->session_count; i++) {
        printf("  [%zu] %s user=%s valid=%d\n", i,
               model->sessions[i].session_id, model->sessions[i].username,
               model->sessions[i].valid);
    }
    printf("Tickets: %zu\n", model->ticket_count);
    for (i = 0; i < model->ticket_count; i++) {
        printf("  [%zu] %s user=%s consumed=%d\n", i,
               model->tickets[i].ticket, model->tickets[i].username,
               model->tickets[i].consumed);
    }
}
