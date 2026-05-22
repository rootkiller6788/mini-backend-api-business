#include "sso_model.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    SsoModel model;
    char session_id[SSO_MAX_SESSION_ID_LEN];
    char assertion[SSO_MAX_SAML_ASSERTION_LEN];
    char ticket[SSO_MAX_TICKET_LEN];
    char username_out[SSO_MAX_USERNAME_LEN];
    char assertion_out[SSO_MAX_SAML_ASSERTION_LEN];
    int rc;

    printf("=== SSO (Single Sign-On) Demo ===\n\n");

    sso_model_init(&model);

    printf("--- IDP & SP Configuration ---\n");

    sso_idp_configure(&model,
        "https://idp.corp.example.com",
        "https://idp.corp.example.com/sso",
        "https://idp.corp.example.com/slo",
        "-----BEGIN CERTIFICATE-----\nMIID...\n-----END CERTIFICATE-----",
        SSO_BINDING_REDIRECT);
    printf("[1] IDP configured: %s\n", model.idp.entity_id);

    rc = sso_register_sp(&model,
        "https://wiki.corp.example.com",
        "https://wiki.corp.example.com/acs",
        "https://wiki.corp.example.com/slo",
        "-----BEGIN CERTIFICATE-----\nMIID...\n-----END CERTIFICATE-----",
        SSO_PROTOCOL_SAML2, SSO_BINDING_POST);
    printf("[2] SP 'wiki' registered: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = sso_register_sp(&model,
        "https://jira.corp.example.com",
        "https://jira.corp.example.com/acs",
        "https://jira.corp.example.com/slo",
        "-----BEGIN CERTIFICATE-----\nMIID...\n-----END CERTIFICATE-----",
        SSO_PROTOCOL_OIDC, SSO_BINDING_REDIRECT);
    printf("[3] SP 'jira' registered: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = sso_register_sp(&model,
        "https://untrusted.example.com",
        "https://untrusted.example.com/acs",
        "",
        "",
        SSO_PROTOCOL_SAML2, SSO_BINDING_REDIRECT);
    printf("[4] SP 'untrusted' registered: %s\n", rc == 0 ? "OK" : "FAIL");

    printf("\n--- User Creation ---\n");

    rc = sso_create_user(&model, "alice", "alice@corp.example.com", "Alice Wang");
    printf("[5] User alice created: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = sso_create_user(&model, "bob", "bob@corp.example.com", "Bob Li");
    printf("[6] User bob created: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = sso_create_user(&model, "charlie", "charlie@corp.example.com", "Charlie Zhang");
    printf("[7] User charlie created: %s\n", rc == 0 ? "OK" : "FAIL");

    printf("\n--- Trust Relationship ---\n");

    rc = sso_establish_trust(&model, "https://wiki.corp.example.com");
    printf("[8] Trust wiki: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = sso_establish_trust(&model, "https://jira.corp.example.com");
    printf("[9] Trust jira: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = sso_authenticate_user(&model, "alice",
        "https://untrusted.example.com",
        session_id, sizeof(session_id),
        assertion, sizeof(assertion));
    printf("[10] Auth alice to untrusted SP: %s (expected FAIL)\n",
           rc == 0 ? "OK" : "FAIL");

    printf("\n--- Authentication + SAML Assertion ---\n");

    rc = sso_authenticate_user(&model, "alice",
        "https://wiki.corp.example.com",
        session_id, sizeof(session_id),
        assertion, sizeof(assertion));
    printf("[11] Auth alice to wiki: %s\n", rc == 0 ? "OK" : "FAIL");
    printf("     Session ID:  %s\n", session_id);
    printf("     SAML Assertion:\n%s\n", assertion);

    rc = sso_validate_session(&model, session_id, username_out, sizeof(username_out));
    printf("[12] Validate session: %s (user=%s)\n",
           rc == 0 ? "VALID" : "INVALID", username_out);

    printf("\n--- Ticket Creation & Validation ---\n");

    rc = sso_create_ticket(&model, "bob",
        "https://wiki.corp.example.com",
        session_id,
        ticket, sizeof(ticket));
    printf("[13] Create ticket for bob at wiki: %s\n", rc == 0 ? "OK" : "FAIL");
    printf("     Ticket: %s\n", ticket);

    rc = sso_validate_ticket(&model, ticket,
        "https://wiki.corp.example.com",
        username_out, sizeof(username_out),
        assertion_out, sizeof(assertion_out));
    printf("[14] Validate ticket: %s (user=%s)\n",
           rc == 0 ? "VALID" : "INVALID", username_out);
    printf("     SAML Assertion:\n%s\n", assertion_out);

    rc = sso_validate_ticket(&model, ticket,
        "https://wiki.corp.example.com",
        NULL, 0, NULL, 0);
    printf("[15] Re-validate consumed ticket: %s (expected FAIL)\n",
           rc == 0 ? "VALID" : "INVALID");

    printf("\n--- Session Federation ---\n");

    rc = sso_authenticate_user(&model, "alice",
        "https://jira.corp.example.com",
        session_id, sizeof(session_id),
        assertion, sizeof(assertion));
    printf("[16] Auth alice to jira (federated session): %s\n",
           rc == 0 ? "OK" : "FAIL");
    printf("     New session: %s\n", session_id);

    rc = sso_validate_session(&model, session_id, username_out, sizeof(username_out));
    printf("[17] Validate federated session: %s\n",
           rc == 0 ? "VALID" : "INVALID");

    printf("\n--- Logout ---\n");

    rc = sso_logout(&model, session_id);
    printf("[18] Logout session: %s\n", rc == 0 ? "OK" : "FAIL");

    rc = sso_validate_session(&model, session_id, NULL, 0);
    printf("[19] Validate after logout: %s\n",
           rc == 0 ? "VALID" : "EXPECTED_INVALID");

    printf("\n--- SAML Assertion Generation ---\n");

    rc = sso_generate_saml_assertion(&model, "charlie",
        "https://wiki.corp.example.com",
        assertion, sizeof(assertion));
    printf("[20] Generate SAML assertion for charlie: %s\n",
           rc == 0 ? "OK" : "FAIL");
    printf("\n%s\n", assertion);

    printf("\n");
    sso_dump_state(&model);

    printf("\n=== All SSO tests completed ===\n");
    return 0;
}
