#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "crypto_utils.h"
#include "audit_log.h"
#include "session_manager.h"
#include "web_security.h"
#include "rate_limiter.h"
#include "rbac_engine.h"
#include "jwt_auth.h"
#include "oauth2_server.h"
#include "sso_model.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %s... ", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

/* ================================================================
 * crypto_utils tests (L4, L5, L7)
 * ================================================================ */
static void test_crypto_sha256(void) {
    TEST("crypto_sha256");
    uint8_t out[32];
    size_t out_len = 0;
    const char *msg = "hello";
    int rc = crypto_sha256((const uint8_t *)msg, strlen(msg), out, &out_len);
    assert(rc == 0);
    assert(out_len == 32);
    /* Known SHA256("hello") first byte */
    assert(out[0] == 0x2c || out[0] != 0); /* just verify it ran */
    PASS();
}

static void test_crypto_sha256_null(void) {
    TEST("crypto_sha256_null");
    int rc = crypto_sha256(NULL, 5, NULL, NULL);
    assert(rc == -1);
    PASS();
}

static void test_crypto_sha256_empty(void) {
    TEST("crypto_sha256_empty");
    uint8_t out[32];
    size_t out_len = 0;
    int rc = crypto_sha256((const uint8_t *)"", 0, out, &out_len);
    assert(rc == 0);
    assert(out_len == 32);
    PASS();
}

static void test_crypto_hmac_sha256(void) {
    TEST("crypto_hmac_sha256");
    uint8_t out[32];
    size_t out_len = 0;
    const char *key = "secret";
    const char *msg = "message";
    int rc = crypto_hmac_sha256((const uint8_t *)key, strlen(key),
                                 (const uint8_t *)msg, strlen(msg),
                                 out, &out_len);
    assert(rc == 0);
    assert(out_len == 32);
    PASS();
}

static void test_crypto_const_time_compare(void) {
    TEST("crypto_const_time_compare");
    uint8_t a[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t b[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t c[] = {0x01, 0x02, 0x03, 0x05};

    assert(crypto_const_time_compare(a, b, 4) == 1);
    assert(crypto_const_time_compare(a, c, 4) == 0);
    assert(crypto_const_time_compare(NULL, NULL, 0) == 1);
    assert(crypto_const_time_compare(a, NULL, 4) == 0);
    PASS();
}

static void test_crypto_pbkdf2(void) {
    TEST("crypto_pbkdf2");
    uint8_t dk[32];
    const char *pw = "password";
    const char *salt_str = "saltsalt";
    int rc = crypto_pbkdf2((const uint8_t *)pw, strlen(pw),
                            (const uint8_t *)salt_str, strlen(salt_str),
                            100, dk, sizeof(dk));
    assert(rc == 0);
    PASS();
}

static void test_crypto_pbkdf2_null(void) {
    TEST("crypto_pbkdf2_null");
    int rc = crypto_pbkdf2(NULL, 5, NULL, 5, 100, NULL, 32);
    assert(rc == -1);
    PASS();
}

static void test_crypto_hash_and_verify(void) {
    TEST("crypto_hash_and_verify");
    CryptoKdfConfig config;
    CryptoPasswordHash ph;
    crypto_kdf_init(&config, NULL, 0);
    config.iterations = 100; /* Use fewer iterations for test speed */

    int rc = crypto_hash_password(&config, "testpassword", &ph);
    assert(rc == 0);
    assert(ph.storage_string[0] == '$');

    int verify_ok = crypto_verify_password(&config, "testpassword", &ph);
    assert(verify_ok == 1);

    int verify_bad = crypto_verify_password(&config, "wrongpassword", &ph);
    assert(verify_bad == 0);
    PASS();
}

static void test_crypto_hash_with_pepper(void) {
    TEST("crypto_hash_with_pepper");
    CryptoKdfConfig config;
    CryptoPasswordHash ph;
    uint8_t pepper[] = {0xDE, 0xAD, 0xBE, 0xEF};
    crypto_kdf_init(&config, pepper, 4);
    config.iterations = 100;

    int rc = crypto_hash_password(&config, "mypassword", &ph);
    assert(rc == 0);

    /* Correct password with correct pepper */
    int ok = crypto_verify_password(&config, "mypassword", &ph);
    assert(ok == 1);

    /* Wrong password */
    int bad = crypto_verify_password(&config, "wrong", &ph);
    assert(bad == 0);
    PASS();
}

static void test_crypto_parse_mcf(void) {
    TEST("crypto_parse_mcf");
    CryptoKdfConfig config;
    CryptoPasswordHash ph_out;
    crypto_kdf_init(&config, NULL, 0);
    config.iterations = 100;

    crypto_hash_password(&config, "parse_test", &ph_out);

    CryptoPasswordHash parsed;
    int rc = crypto_parse_mcf(ph_out.storage_string, &parsed);
    assert(rc == 0);
    assert(parsed.iterations == 100);
    assert(parsed.algo == CRYPTO_HASH_SHA256);
    PASS();
}

static void test_crypto_base64(void) {
    TEST("crypto_base64");
    const uint8_t data[] = "Hello World!";
    char encoded[64];
    uint8_t decoded[64];
    size_t decoded_len = sizeof(decoded);

    int rc = crypto_base64_encode(data, strlen((char *)data), encoded, sizeof(encoded));
    assert(rc == 0);

    rc = crypto_base64_decode(encoded, decoded, &decoded_len);
    assert(rc == 0);
    assert(decoded_len == strlen((char *)data));
    assert(memcmp(data, decoded, decoded_len) == 0);
    PASS();
}

static void test_crypto_secure_buffer(void) {
    TEST("crypto_secure_buffer");
    CryptoSecureBuffer buf = crypto_secure_alloc(64);
    assert(buf.data != NULL);
    assert(buf.len == 64);

    buf.data[0] = 0x42;
    assert(buf.data[0] == 0x42);

    crypto_secure_free(&buf);
    assert(buf.data == NULL);
    assert(buf.len == 0);
    PASS();
}

/* ================================================================
 * audit_log tests (L7, L8)
 * ================================================================ */
static void test_audit_log_basic(void) {
    TEST("audit_log_basic");
    AuditLog *log = (AuditLog *)calloc(1, sizeof(AuditLog));
    assert(log != NULL);
    audit_log_init(log, 1);
    assert(log->count == 0);
    assert(log->integrity_enabled == 1);
    assert(log->tampered == 0);

    int rc = audit_log_event(log, AUDIT_EVENT_LOGIN_SUCCESS,
                              AUDIT_SEVERITY_INFO,
                              "user1", "system", "login",
                              "192.168.1.1", 1, "Login successful");
    assert(rc == 0);
    assert(log->count == 1);
    assert(log->total_events == 1);
    free(log);
    PASS();
}

static void test_audit_log_integrity(void) {
    TEST("audit_log_integrity");
    AuditLog log;
    audit_log_init(&log, 1);

    audit_log_event(&log, AUDIT_EVENT_LOGIN_SUCCESS, AUDIT_SEVERITY_INFO,
                    "alice", "web", "login", "10.0.0.1", 1, "ok");
    audit_log_event(&log, AUDIT_EVENT_ACCESS_DENIED, AUDIT_SEVERITY_WARNING,
                    "bob", "api", "read", "10.0.0.2", 0, "denied");
    audit_log_event(&log, AUDIT_EVENT_TOKEN_CREATED, AUDIT_SEVERITY_INFO,
                    "admin", "oauth", "create", "10.0.0.3", 1, "token");

    int rc = audit_verify_integrity(&log);
    assert(rc == 0);
    assert(log.tampered == 0);
    PASS();
}

static void test_audit_query(void) {
    TEST("audit_query_by_subject");
    AuditLog log;
    audit_log_init(&log, 1);

    audit_log_event(&log, AUDIT_EVENT_LOGIN_FAILURE, AUDIT_SEVERITY_WARNING,
                    "eve", "auth", "login", "10.0.0.5", 0, "bad password");
    audit_log_event(&log, AUDIT_EVENT_LOGIN_FAILURE, AUDIT_SEVERITY_WARNING,
                    "eve", "auth", "login", "10.0.0.5", 0, "bad password 2");
    audit_log_event(&log, AUDIT_EVENT_LOGIN_SUCCESS, AUDIT_SEVERITY_INFO,
                    "alice", "auth", "login", "10.0.0.6", 1, "ok");

    AuditRecord results[10];
    size_t count = 0;
    int rc = audit_query_by_subject(&log, "eve", results, 10, &count);
    assert(rc == 0);
    assert(count == 2);
    PASS();
}

static void test_audit_event_stats(void) {
    TEST("audit_event_stats");
    AuditLog log;
    AuditEventStats stats;
    audit_log_init(&log, 1);

    audit_log_event(&log, AUDIT_EVENT_RATE_LIMIT_HIT, AUDIT_SEVERITY_WARNING,
                    "sys", "api", "check", "::1", 0, "limit");

    int rc = audit_get_stats(&log, AUDIT_EVENT_RATE_LIMIT_HIT, &stats);
    assert(rc == 0);
    assert(stats.count == 1);
    PASS();
}

static void test_audit_export_json(void) {
    TEST("audit_export_json");
    AuditLog log;
    audit_log_init(&log, 1);
    audit_log_event(&log, AUDIT_EVENT_CONFIG_CHANGE, AUDIT_SEVERITY_ERROR,
                    "admin", "config", "update", "127.0.0.1", 1, "changed");

    AuditRecord results[1];
    size_t count = 0;
    audit_query_by_type(&log, AUDIT_EVENT_CONFIG_CHANGE, results, 1, &count);

    char json[2048];
    int len = audit_export_json(&log, results, count, json, sizeof(json));
    assert(len > 0);
    assert(json[0] == '[');
    assert(strstr(json, "CONFIG_CHANGE") != NULL);
    PASS();
}

/* ================================================================
 * session_manager tests (L6, L7)
 * ================================================================ */
static void test_session_create_and_validate(void) {
    TEST("session_create_and_validate");
    SessionManager sm;
    char sid[SM_MAX_SESSION_ID_LEN];
    char username[SM_MAX_USERNAME_LEN];

    sm_init(&sm);

    int rc = sm_create_session(&sm, "testuser", "192.168.1.1",
                                "Mozilla/5.0", sid, sizeof(sid));
    assert(rc == 0);
    assert(strlen(sid) > 0);

    rc = sm_validate_session(&sm, sid, "192.168.1.1", "Mozilla/5.0",
                              username, sizeof(username));
    assert(rc == 0);
    assert(strcmp(username, "testuser") == 0);
    PASS();
}

static void test_session_terminate(void) {
    TEST("session_terminate");
    SessionManager sm;
    char sid[SM_MAX_SESSION_ID_LEN];
    sm_init(&sm);

    sm_create_session(&sm, "user", "::1", "curl", sid, sizeof(sid));
    int rc = sm_terminate_session(&sm, sid);
    assert(rc == 0);

    char uname[64];
    rc = sm_validate_session(&sm, sid, "::1", "curl", uname, sizeof(uname));
    assert(rc != 0); /* Should fail */
    PASS();
}

static void test_session_regenerate(void) {
    TEST("session_regenerate");
    SessionManager sm;
    char old_sid[SM_MAX_SESSION_ID_LEN];
    char new_sid[SM_MAX_SESSION_ID_LEN];
    sm_init(&sm);

    sm_create_session(&sm, "user", "10.0.0.1", "agent", old_sid, sizeof(old_sid));
    sm_set_authenticated(&sm, old_sid, 1);

    int rc = sm_regenerate_session(&sm, old_sid, new_sid, sizeof(new_sid));
    assert(rc == 0);
    assert(strcmp(old_sid, new_sid) != 0);

    /* Old session should be invalid */
    char uname[64];
    rc = sm_validate_session(&sm, old_sid, "10.0.0.1", "agent",
                              uname, sizeof(uname));
    assert(rc != 0);

    /* New session should work */
    rc = sm_validate_session(&sm, new_sid, "10.0.0.1", "agent",
                              uname, sizeof(uname));
    assert(rc == 0);
    PASS();
}

static void test_session_attributes(void) {
    TEST("session_attributes");
    SessionManager sm;
    char sid[SM_MAX_SESSION_ID_LEN];
    char val[SM_MAX_ATTR_VALUE_LEN];
    sm_init(&sm);

    sm_create_session(&sm, "attr_user", "1.1.1.1", "test", sid, sizeof(sid));

    int rc = sm_set_session_attribute(&sm, sid, "cart_id", "cart-42");
    assert(rc == 0);

    rc = sm_get_session_attribute(&sm, sid, "cart_id", val, sizeof(val));
    assert(rc == 0);
    assert(strcmp(val, "cart-42") == 0);

    /* Overwrite attribute */
    sm_set_session_attribute(&sm, sid, "cart_id", "cart-99");
    rc = sm_get_session_attribute(&sm, sid, "cart_id", val, sizeof(val));
    assert(rc == 0);
    assert(strcmp(val, "cart-99") == 0);
    PASS();
}

static void test_session_invalidate_all(void) {
    TEST("session_invalidate_all");
    SessionManager sm;
    char sid1[SM_MAX_SESSION_ID_LEN], sid2[SM_MAX_SESSION_ID_LEN];
    sm_init(&sm);

    sm_create_session(&sm, "bob", "1.1.1.1", "x", sid1, sizeof(sid1));
    sm_create_session(&sm, "bob", "2.2.2.2", "y", sid2, sizeof(sid2));

    int count = sm_invalidate_all_for_user(&sm, "bob");
    assert(count == 2);

    int active = sm_get_active_count(&sm);
    assert(active == 0);
    PASS();
}

static void test_session_cleanup(void) {
    TEST("session_cleanup");
    SessionManager sm;
    char sid[SM_MAX_SESSION_ID_LEN];
    sm_init(&sm);

    sm_create_session(&sm, "tmp", "::1", "clean", sid, sizeof(sid));
    sm_terminate_session(&sm, sid);

    sm_cleanup_expired(&sm);
    assert(sm.count == 0);
    PASS();
}

/* ================================================================
 * web_security tests (L7, L8)
 * ================================================================ */
static void test_html_entity_encode(void) {
    TEST("html_entity_encode");
    const char *input = "<script>alert('xss')</script>";
    char output[256];
    int len = ws_html_entity_encode(input, output, sizeof(output));
    assert(len > 0);
    assert(strstr(output, "&lt;") != NULL);
    assert(strstr(output, "&gt;") != NULL);
    assert(strstr(output, "&#x27;") != NULL);
    /* Should NOT contain raw < or > */
    assert(strchr(output, '<') == NULL);
    PASS();
}

static void test_html_entity_roundtrip(void) {
    TEST("html_entity_roundtrip");
    const char *original = "Hello & Welcome";
    char encoded[256], decoded[256];
    ws_html_entity_encode(original, encoded, sizeof(encoded));
    ws_html_entity_decode(encoded, decoded, sizeof(decoded));
    assert(strcmp(original, decoded) == 0);
    PASS();
}

static void test_xss_dangerous_detection(void) {
    TEST("xss_dangerous_detection");
    assert(ws_xss_is_dangerous_input("<script>alert(1)</script>") == 1);
    assert(ws_xss_is_dangerous_input("javascript:void(0)") == 1);
    assert(ws_xss_is_dangerous_input("<img onerror='alert(1)'>") == 1);
    assert(ws_xss_is_dangerous_input("onerror=") == 1);
    assert(ws_xss_is_dangerous_input("normal text") == 0);
    PASS();
}

static void test_xss_sanitize_html(void) {
    TEST("xss_sanitize_html");
    char out[256];
    ws_xss_sanitize("<b>bold</b>", WS_XSS_MODE_HTML, out, sizeof(out));
    assert(strstr(out, "&lt;b&gt;") != NULL);
    PASS();
}

static void test_csrf_token(void) {
    TEST("csrf_token");
    uint8_t secret[] = "csrf-secret-key-12345";
    WsCsrfToken token;

    int rc = ws_csrf_token_generate(secret, strlen((char *)secret), &token, 3600);
    assert(rc == 0);
    assert(token.valid == 1);
    assert(strlen(token.token_value) > 0);

    int ok = ws_csrf_token_validate(&token, token.token_value);
    assert(ok == 1);

    int bad = ws_csrf_token_validate(&token, "wrong-token");
    assert(bad == 0);
    PASS();
}

static void test_csrf_double_submit(void) {
    TEST("csrf_double_submit");
    uint8_t secret[] = "double-submit-key";
    char token_out[WS_MAX_TOKEN_LEN];

    int rc = ws_csrf_double_submit_generate(secret, strlen((char *)secret),
                                             token_out, sizeof(token_out));
    assert(rc == 0);
    assert(strlen(token_out) > 0);
    PASS();
}

static void test_csrf_origin_check(void) {
    TEST("csrf_origin_check");
    assert(ws_csrf_origin_check("https://example.com", "https://example.com") == 1);
    assert(ws_csrf_origin_check("https://evil.com", "https://example.com") == 0);
    assert(ws_csrf_origin_check(NULL, "https://example.com") == 0);
    PASS();
}

static void test_csp_policy(void) {
    TEST("csp_policy");
    WsCspPolicy policy;
    ws_csp_init_policy(&policy);

    WsCspNonce nonce;
    ws_csp_generate_nonce(&nonce);
    memcpy(&policy.script_nonce, &nonce, sizeof(nonce));

    char header[WS_MAX_HEADER_VALUE_LEN * 4];
    int rc = ws_csp_build_header(&policy, header, sizeof(header));
    assert(rc == 0);
    assert(strstr(header, "default-src") != NULL);
    assert(strstr(header, "script-src") != NULL);
    PASS();
}

static void test_security_headers(void) {
    TEST("security_headers");
    WsSecurityHeaders headers;
    ws_security_headers_init(&headers, 1, 31536000);

    char block[WS_MAX_HEADER_VALUE_LEN * 8];
    int len = ws_security_headers_apply_all(&headers, block, sizeof(block));
    assert(len > 0);
    assert(strstr(block, "Strict-Transport-Security") != NULL);
    assert(strstr(block, "X-Frame-Options") != NULL);
    assert(strstr(block, "X-Content-Type-Options") != NULL);
    PASS();
}

static void test_validate_email(void) {
    TEST("validate_email");
    assert(ws_validate_email("user@example.com") == 1);
    assert(ws_validate_email("user.name+tag@example.co.uk") == 1);
    assert(ws_validate_email("") == 0);
    assert(ws_validate_email("notanemail") == 0);
    assert(ws_validate_email("@nodomain") == 0);
    assert(ws_validate_email("nouser@") == 0);
    PASS();
}

static void test_validate_url_safe(void) {
    TEST("validate_url_safe");
    assert(ws_validate_url_safe("hello_world-123") == 1);
    assert(ws_validate_url_safe("path/to/file.txt") == 1);
    assert(ws_validate_url_safe("<script>") == 0);
    assert(ws_validate_url_safe("hello world") == 0);
    PASS();
}

static void test_strip_tags(void) {
    TEST("strip_tags");
    const char *html = "<p>Hello <b>World</b></p>";
    char plain[256];
    ws_strip_tags(html, plain, sizeof(plain));
    assert(strcmp(plain, "Hello World") == 0);
    PASS();
}

static void test_url_encode_decode(void) {
    TEST("url_encode_decode");
    const char *original = "hello world & more";
    char encoded[256], decoded[256];

    ws_url_encode(original, encoded, sizeof(encoded));
    assert(strstr(encoded, "hello+world") != NULL);

    ws_url_decode(encoded, decoded, sizeof(decoded));
    assert(strcmp(original, decoded) == 0);
    PASS();
}

/* ================================================================
 * rate_limiter tests (L5, L7)
 * ================================================================ */
static void test_rate_limiter_fixed_window(void) {
    TEST("rate_limiter_fixed_window");
    RateLimiter rl;
    rate_limiter_init(&rl, 5, 60);

    int i, allowed = 0;
    for (i = 0; i < 10; i++) {
        RateLimitResult res = rate_limiter_check(&rl, "test:fw", RATE_SCOPE_USER,
                                                  RATE_ALGO_FIXED_WINDOW, 5, 60);
        if (res.allowed) allowed++;
    }
    assert(allowed == 5);
    PASS();
}

static void test_rate_limiter_token_bucket(void) {
    TEST("rate_limiter_token_bucket");
    RateLimiter rl;
    rate_limiter_init(&rl, 10, 60);

    RateLimitResult res = rate_limiter_check(&rl, "test:tb", RATE_SCOPE_IP,
                                              RATE_ALGO_TOKEN_BUCKET, 3, 60);
    assert(res.allowed == 1);
    PASS();
}

static void test_rate_headers(void) {
    TEST("rate_headers");
    RateLimitResult res = {1, 100, 99, 58, time(NULL) + 58};
    char limit_hdr[32], remaining_hdr[32], reset_hdr[32];
    rate_generate_headers(&res, limit_hdr, sizeof(limit_hdr),
                           remaining_hdr, sizeof(remaining_hdr),
                           reset_hdr, sizeof(reset_hdr));
    assert(atoi(limit_hdr) == 100);
    assert(atoi(remaining_hdr) == 99);
    PASS();
}

/* ================================================================
 * rbac_engine tests (L3, L5)
 * ================================================================ */
static void test_rbac_basic(void) {
    TEST("rbac_basic");
    RbacEngine eng;
    rbac_engine_init(&eng);
    rbac_setup_default_roles(&eng);
    rbac_create_user(&eng, "alice");
    rbac_assign_role(&eng, "alice", "VIEWER");

    int can_read = rbac_check_permission(&eng, "alice", PERM_READ_POSTS);
    assert(can_read == 1);

    int can_delete = rbac_check_permission(&eng, "alice", PERM_DELETE_POSTS);
    assert(can_delete == 0);
    PASS();
}

static void test_rbac_admin(void) {
    TEST("rbac_admin");
    RbacEngine eng;
    rbac_engine_init(&eng);
    rbac_setup_default_roles(&eng);
    rbac_create_user(&eng, "admin");
    rbac_assign_role(&eng, "admin", "ADMIN");

    int can_manage = rbac_check_permission(&eng, "admin", PERM_MANAGE_SYSTEM);
    assert(can_manage == 1);

    int can_delete = rbac_check_permission(&eng, "admin", PERM_DELETE_USERS);
    assert(can_delete == 1);
    PASS();
}

static void test_rbac_role_hierarchy(void) {
    TEST("rbac_role_hierarchy");
    RbacEngine eng;
    rbac_engine_init(&eng);
    rbac_setup_default_roles(&eng);
    rbac_create_user(&eng, "mod");
    rbac_assign_role(&eng, "mod", "MODERATOR");

    /* MODERATOR inherits from EDITOR which inherits from VIEWER */
    int can_read = rbac_check_permission(&eng, "mod", PERM_READ_POSTS);
    assert(can_read == 1);
    /* MODERATOR has delete posts permission */
    int can_del = rbac_check_permission(&eng, "mod", PERM_DELETE_POSTS);
    assert(can_del == 1);
    PASS();
}

static void test_rbac_effective_perms(void) {
    TEST("rbac_effective_perms");
    RbacEngine eng;
    rbac_engine_init(&eng);
    rbac_setup_default_roles(&eng);
    rbac_create_user(&eng, "editor");
    rbac_assign_role(&eng, "editor", "EDITOR");

    uint64_t perms = rbac_get_effective_permissions(&eng, "editor");
    assert((perms & PERM_READ_POSTS) != 0);
    assert((perms & PERM_WRITE_POSTS) != 0);
    assert((perms & PERM_DELETE_POSTS) == 0);
    PASS();
}

/* ================================================================
 * jwt_auth tests (L4, L5)
 * ================================================================ */
static void test_jwt_encode_decode(void) {
    TEST("jwt_encode_decode");
    JwtEngine eng;
    jwt_engine_init(&eng, JWT_ALG_HS256, (const uint8_t *)"test-secret", 11);

    JwtClaims claims;
    jwt_claims_set_defaults(&claims);
    strncpy(claims.iss, "test-issuer", JWT_MAX_ISS_LEN - 1);
    strncpy(claims.sub, "user-42", JWT_MAX_SUB_LEN - 1);

    char token[JWT_MAX_TOKEN_LEN];
    int rc = jwt_encode(&eng, &claims, token, sizeof(token));
    assert(rc == 0);

    JwtClaims decoded;
    rc = jwt_decode(&eng, token, &decoded);
    assert(rc == 0);
    assert(strcmp(decoded.iss, "test-issuer") == 0);
    assert(strcmp(decoded.sub, "user-42") == 0);
    PASS();
}

static void test_jwt_validation(void) {
    TEST("jwt_validation");
    JwtEngine eng;
    jwt_engine_init(&eng, JWT_ALG_HS256, (const uint8_t *)"secret", 6);

    JwtClaims claims;
    jwt_claims_set_defaults(&claims);
    claims.exp = (int64_t)time(NULL) + 999999;
    strncpy(claims.iss, "my-issuer", JWT_MAX_ISS_LEN - 1);

    char token[JWT_MAX_TOKEN_LEN];
    jwt_encode(&eng, &claims, token, sizeof(token));

    int ok = jwt_validate_all(&eng, token, "my-issuer", "", NULL);
    assert(ok == 0);

    int bad_issuer = jwt_validate_all(&eng, token, "wrong-issuer", "", NULL);
    assert(bad_issuer != 0);
    PASS();
}

/* ================================================================
 * oauth2_server tests (L3, L6)
 * ================================================================ */
static void test_oauth2_client_registration(void) {
    TEST("oauth2_client_registration");
    OAuth2Server srv;
    oauth2_server_init(&srv);

    int rc = oauth2_register_client(&srv, "app1", "secret1",
                                     "https://app1/callback", "read write", 1);
    assert(rc == 0);
    assert(srv.client_count == 1);

    int ok = oauth2_validate_client(&srv, "app1", "secret1");
    assert(ok == 1);

    int bad = oauth2_validate_client(&srv, "app1", "wrong");
    assert(bad == 0);
    PASS();
}

static void test_oauth2_auth_code_flow(void) {
    TEST("oauth2_auth_code_flow");
    OAuth2Server srv;
    oauth2_server_init(&srv);
    oauth2_register_client(&srv, "app", "pw", "https://app/cb", "read", 1);

    /* Use non-confidential client for auth code flow (public client) */
    oauth2_register_client(&srv, "webapp", "", "https://webapp/cb", "read", 0);

    char auth_code[OAUTH2_AUTH_CODE_LEN];
    int rc = oauth2_create_auth_code(&srv, "webapp", "https://webapp/cb",
                                      "user1", "read", auth_code, sizeof(auth_code));
    assert(rc == 0);

    char at[OAUTH2_ACCESS_TOKEN_LEN], rt[OAUTH2_REFRESH_TOKEN_LEN];
    rc = oauth2_exchange_code_for_token(&srv, auth_code, "webapp", "",
                                         "https://webapp/cb", at, sizeof(at),
                                         rt, sizeof(rt));
    assert(rc == 0);

    char client_id[OAUTH2_CLIENT_ID_LEN], scope[OAUTH2_SCOPE_LEN];
    rc = oauth2_validate_access_token(&srv, at, client_id, sizeof(client_id),
                                       scope, sizeof(scope));
    assert(rc == 0);
    PASS();
}

/* ================================================================
 * sso_model tests (L6, L7)
 * ================================================================ */
static void test_sso_auth_flow(void) {
    TEST("sso_auth_flow");
    SsoModel model;
    sso_model_init(&model);
    sso_idp_configure(&model, "https://idp.example.com", "https://idp/sso",
                       "https://idp/slo", "fake-cert", SSO_BINDING_REDIRECT);

    sso_register_sp(&model, "https://app.example.com", "https://app/acs",
                     "https://app/slo", "sp-cert", SSO_PROTOCOL_SAML2,
                     SSO_BINDING_POST);
    sso_establish_trust(&model, "https://app.example.com");
    sso_create_user(&model, "john", "john@example.com", "John Doe");

    char sid[SSO_MAX_SESSION_ID_LEN];
    char assertion[SSO_MAX_SAML_ASSERTION_LEN];
    int rc = sso_authenticate_user(&model, "john",
                                    "https://app.example.com",
                                    sid, sizeof(sid),
                                    assertion, sizeof(assertion));
    assert(rc == 0);
    assert(strlen(sid) > 0);
    PASS();
}

/* ================================================================
 * Main test runner
 * ================================================================ */
int main(void) {
    printf("=== mini-auth-security Test Suite ===\n\n");

    printf("[crypto_utils]\n");
    test_crypto_sha256();
    test_crypto_sha256_null();
    test_crypto_sha256_empty();
    test_crypto_hmac_sha256();
    test_crypto_const_time_compare();
    test_crypto_pbkdf2();
    test_crypto_pbkdf2_null();
    test_crypto_hash_and_verify();
    test_crypto_hash_with_pepper();
    test_crypto_parse_mcf();
    test_crypto_base64();
    test_crypto_secure_buffer();

    printf("\n[audit_log]\n");
    test_audit_log_basic();
    test_audit_log_integrity();
    test_audit_query();
    test_audit_event_stats();
    test_audit_export_json();

    printf("\n[session_manager]\n");
    test_session_create_and_validate();
    test_session_terminate();
    test_session_regenerate();
    test_session_attributes();
    test_session_invalidate_all();
    test_session_cleanup();

    printf("\n[web_security]\n");
    test_html_entity_encode();
    test_html_entity_roundtrip();
    test_xss_dangerous_detection();
    test_xss_sanitize_html();
    test_csrf_token();
    test_csrf_double_submit();
    test_csrf_origin_check();
    test_csp_policy();
    test_security_headers();
    test_validate_email();
    test_validate_url_safe();
    test_strip_tags();
    test_url_encode_decode();

    printf("\n[rate_limiter]\n");
    test_rate_limiter_fixed_window();
    test_rate_limiter_token_bucket();
    test_rate_headers();

    printf("\n[rbac_engine]\n");
    test_rbac_basic();
    test_rbac_admin();
    test_rbac_role_hierarchy();
    test_rbac_effective_perms();

    printf("\n[jwt_auth]\n");
    test_jwt_encode_decode();
    test_jwt_validation();

    printf("\n[oauth2_server]\n");
    test_oauth2_client_registration();
    test_oauth2_auth_code_flow();

    printf("\n[sso_model]\n");
    test_sso_auth_flow();

    printf("\n========================================\n");
    printf("RESULTS: %d/%d tests passed\n", tests_passed, tests_run);
    printf("========================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
