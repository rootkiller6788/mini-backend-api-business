#ifndef SESSION_H
#define SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "http_core.h"

/*
 * L1 - Core Definitions: Session struct, Cookie attributes
 * L2 - Core Concepts: HTTP state management (RFC 6265), server-side sessions
 * L3 - Engineering Structures: In-memory session store with TTL expiry
 * L4 - Standards/Theorems: RFC 6265 HTTP State Management Mechanism
 * L5 - Algorithms: Session ID generation (non-crypto UUID-like),
 *      LRU-style eviction for memory-bounded stores
 * L6 - Canonical Problem: Web authentication state persistence
 * L7 - Application: User login sessions, shopping carts, CSRF tokens
 * L8 - Advanced: Distributed session stores (Redis), JWT stateless sessions
 * L9 - Industry: OAuth 2.0 / OpenID Connect session federation
 */

#define SESSION_MAX_COUNT        1024
#define SESSION_ID_LEN            64
#define SESSION_MAX_KEY_LEN      128
#define SESSION_MAX_VAL_LEN     4096
#define SESSION_MAX_VARS          32
#define SESSION_DEFAULT_TTL     1800

/* ── Session Variable ────────────────────────────────────────────────────── */
typedef struct {
    char key[SESSION_MAX_KEY_LEN];
    char value[SESSION_MAX_VAL_LEN];
} SessionVar;

/* ── Session ─────────────────────────────────────────────────────────────── */
typedef struct {
    char        id[SESSION_ID_LEN];
    SessionVar  vars[SESSION_MAX_VARS];
    int         var_count;
    time_t      created_at;
    time_t      last_access;
    time_t      expires_at;
    bool        active;
} Session;

/* ── Session Store ───────────────────────────────────────────────────────── */
typedef struct {
    Session     sessions[SESSION_MAX_COUNT];
    int         count;
    int         default_ttl;
    char        cookie_name[64];
    char        cookie_path[256];
    char        cookie_domain[256];
    bool        cookie_secure;
    bool        cookie_http_only;
} SessionStore;

/* ── Store Management ────────────────────────────────────────────────────── */
void session_store_init(SessionStore *store);
void session_store_set_ttl(SessionStore *store, int ttl_seconds);
void session_store_set_cookie(SessionStore *store, const char *name,
                               const char *path, const char *domain,
                               bool secure, bool http_only);

/* ── Session CRUD ────────────────────────────────────────────────────────── */
Session *session_create(SessionStore *store);
Session *session_get(SessionStore *store, const char *session_id);
bool     session_destroy(SessionStore *store, const char *session_id);

/* ── Session Variables ───────────────────────────────────────────────────── */
bool session_var_set(Session *s, const char *key, const char *value);
const char *session_var_get(const Session *s, const char *key);
bool session_var_remove(Session *s, const char *key);
void session_clear(Session *s);

/* ── Cookie Helpers ──────────────────────────────────────────────────────── */
/* Extract session ID from Cookie header */
bool session_parse_cookie(const HttpRequest *req, const char *cookie_name,
                           char *session_id, size_t id_sz);

/* Build Set-Cookie response header */
void session_build_set_cookie(const SessionStore *store, const Session *s,
                               HttpResponse *res);

/* Clean expired sessions (call periodically) */
int  session_store_cleanup(SessionStore *store);

/* ── Session Middleware ──────────────────────────────────────────────────── */
#include "middleware.h"
MiddlewareResult session_middleware_loader(HttpRequest *req,
                                            HttpResponse *res,
                                            MiddlewareContext *ctx);

#endif /* SESSION_H */
