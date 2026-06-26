#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * L5: Session ID generation.
 * Uses a combination of time, counter, and random values to generate
 * a unique session identifier. Not cryptographically secure - suitable
 * for non-security-critical applications (L9 note: production systems
 * should use CSPRNG-based IDs).
 */
static void generate_session_id(char *buf, size_t buf_sz) {
    static uint64_t counter = 0;
    uint64_t now = (uint64_t)time(NULL);
    uint64_t rnd = (uint64_t)rand();
    counter++;

    snprintf(buf, buf_sz, "%08lx%04lx%04lx%04lx%012lx",
             (unsigned long)(now & 0xFFFFFFFF),
             (unsigned long)((now >> 32) & 0xFFFF),
             (unsigned long)(rnd & 0xFFFF),
             (unsigned long)((rnd >> 16) & 0xFFFF),
             (unsigned long)counter);
}

/* ── Store Management ────────────────────────────────────────────────────── */

void session_store_init(SessionStore *store) {
    memset(store, 0, sizeof(*store));
    store->default_ttl = SESSION_DEFAULT_TTL;
    strncpy(store->cookie_name, "SESSID", sizeof(store->cookie_name) - 1);
    strncpy(store->cookie_path, "/", sizeof(store->cookie_path) - 1);
    store->cookie_http_only = 1;
    store->cookie_secure = 0;

    /* Seed random number generator */
    srand((unsigned int)time(NULL));
}

void session_store_set_ttl(SessionStore *store, int ttl_seconds) {
    if (store && ttl_seconds > 0) store->default_ttl = ttl_seconds;
}

void session_store_set_cookie(SessionStore *store, const char *name,
                               const char *path, const char *domain,
                               bool secure, bool http_only) {
    if (!store) return;
    if (name)   strncpy(store->cookie_name, name, sizeof(store->cookie_name) - 1);
    if (path)   strncpy(store->cookie_path, path, sizeof(store->cookie_path) - 1);
    if (domain) strncpy(store->cookie_domain, domain, sizeof(store->cookie_domain) - 1);
    store->cookie_secure    = secure;
    store->cookie_http_only = http_only;
}

/* ── Session CRUD ────────────────────────────────────────────────────────── */

Session *session_create(SessionStore *store) {
    if (!store || store->count >= SESSION_MAX_COUNT) return NULL;

    Session *s = &store->sessions[store->count];
    memset(s, 0, sizeof(*s));

    generate_session_id(s->id, sizeof(s->id));

    s->created_at  = time(NULL);
    s->last_access = s->created_at;
    s->expires_at  = s->created_at + store->default_ttl;
    s->active      = 1;
    store->count++;

    return s;
}

Session *session_get(SessionStore *store, const char *session_id) {
    if (!store || !session_id) return NULL;

    time_t now = time(NULL);
    for (int i = 0; i < store->count; i++) {
        Session *s = &store->sessions[i];
        if (!s->active) continue;
        if (s->expires_at < now) {
            s->active = 0;
            continue;
        }
        if (strcmp(s->id, session_id) == 0) {
            s->last_access = now;
            return s;
        }
    }
    return NULL;
}

bool session_destroy(SessionStore *store, const char *session_id) {
    Session *s = session_get(store, session_id);
    if (!s) return 0;
    s->active = 0;
    return 1;
}

/* ── Session Variables ───────────────────────────────────────────────────── */

bool session_var_set(Session *s, const char *key, const char *value) {
    if (!s || !key || !value) return 0;

    /* Update existing key */
    for (int i = 0; i < s->var_count; i++) {
        if (strcmp(s->vars[i].key, key) == 0) {
            strncpy(s->vars[i].value, value, SESSION_MAX_VAL_LEN - 1);
            s->vars[i].value[SESSION_MAX_VAL_LEN - 1] = '\0';
            return 1;
        }
    }

    /* Add new variable */
    if (s->var_count >= SESSION_MAX_VARS) return 0;
    strncpy(s->vars[s->var_count].key, key, SESSION_MAX_KEY_LEN - 1);
    strncpy(s->vars[s->var_count].value, value, SESSION_MAX_VAL_LEN - 1);
    s->var_count++;
    return 1;
}

const char *session_var_get(const Session *s, const char *key) {
    if (!s || !key) return NULL;
    for (int i = 0; i < s->var_count; i++) {
        if (strcmp(s->vars[i].key, key) == 0)
            return s->vars[i].value;
    }
    return NULL;
}

bool session_var_remove(Session *s, const char *key) {
    if (!s || !key) return 0;
    for (int i = 0; i < s->var_count; i++) {
        if (strcmp(s->vars[i].key, key) == 0) {
            /* Move last element into this slot */
            if (i < s->var_count - 1) {
                s->vars[i] = s->vars[s->var_count - 1];
            }
            s->var_count--;
            return 1;
        }
    }
    return 0;
}

void session_clear(Session *s) {
    if (s) s->var_count = 0;
}

/* ── Cookie Parsing (RFC 6265 Sec 4.2.1) ──────────────────────────────────── */
/*
 * L4: RFC 6265 cookie header parsing.
 * Cookie: name1=value1; name2=value2
 * Handles quoted values, whitespace, and edge cases.
 */
bool session_parse_cookie(const HttpRequest *req, const char *cookie_name,
                           char *session_id, size_t id_sz) {
    if (!req || !cookie_name || !session_id || id_sz == 0) return 0;

    const char *cookie_hdr = http_request_get_header(req, "Cookie");
    if (!cookie_hdr) return 0;

    const char *p = cookie_hdr;
    size_t name_len = strlen(cookie_name);

    while (*p) {
        /* Skip leading whitespace and semicolons */
        while (*p == ' ' || *p == ';') p++;
        if (!*p) break;

        /* Check if this is our cookie */
        if (strncmp(p, cookie_name, name_len) == 0 && p[name_len] == '=') {
            p += name_len + 1;

            /* Skip whitespace after = */
            while (*p == ' ') p++;

            /* Extract value until ; or end */
            size_t i = 0;
            if (*p == '"') {
                p++; /* skip opening quote */
                while (*p && *p != '"' && *p != ';' && i < id_sz - 1) {
                    session_id[i++] = *p++;
                }
                if (*p == '"') p++;
            } else {
                while (*p && *p != ';' && *p != ' ' && i < id_sz - 1) {
                    session_id[i++] = *p++;
                }
            }
            session_id[i] = '\0';
            return i > 0;
        }

        /* Skip to next cookie */
        while (*p && *p != ';') p++;
    }
    return 0;
}

/* ── Set-Cookie Builder (RFC 6265 Sec 4.1.1) ──────────────────────────────── */
void session_build_set_cookie(const SessionStore *store, const Session *s,
                               HttpResponse *res) {
    if (!store || !s || !res) return;

    char cookie_val[512];
    int off = snprintf(cookie_val, sizeof(cookie_val), "%s=%s", store->cookie_name, s->id);

    if (store->cookie_path[0]) {
        off += snprintf(cookie_val + off, sizeof(cookie_val) - off, "; Path=%s", store->cookie_path);
    }
    if (store->cookie_domain[0]) {
        off += snprintf(cookie_val + off, sizeof(cookie_val) - off, "; Domain=%s", store->cookie_domain);
    }

    char expires[64];
    struct tm *tm_info = gmtime(&s->expires_at);
    strftime(expires, sizeof(expires), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    off += snprintf(cookie_val + off, sizeof(cookie_val) - off, "; Expires=%s", expires);

    if (store->cookie_secure) {
        off += snprintf(cookie_val + off, sizeof(cookie_val) - off, "; Secure");
    }
    if (store->cookie_http_only) {
        off += snprintf(cookie_val + off, sizeof(cookie_val) - off, "; HttpOnly");
    }
    off += snprintf(cookie_val + off, sizeof(cookie_val) - off, "; SameSite=Lax");

    http_response_add_header(res, "Set-Cookie", cookie_val);
}

/* ── Session Cleanup ─────────────────────────────────────────────────────── */
/*
 * L5: TTL-based cleanup with O(n) scan.
 * For larger stores, a priority queue by expiry time would give O(log n) eviction.
 */
int session_store_cleanup(SessionStore *store) {
    if (!store) return 0;

    int removed = 0;
    time_t now = time(NULL);

    for (int i = store->count - 1; i >= 0; i--) {
        if (!store->sessions[i].active || store->sessions[i].expires_at < now) {
            store->sessions[i].active = 0;

            /* Compact: swap with last active */
            if (i < store->count - 1) {
                store->sessions[i] = store->sessions[store->count - 1];
            }
            store->count--;
            removed++;
        }
    }
    return removed;
}

/* ── Session Middleware ──────────────────────────────────────────────────── */
/*
 * L7: Session middleware integrates cookie parsing + session lookup
 * into the middleware pipeline. Stores Session* in ctx->user_data
 * for downstream handlers to access session variables.
 *
 * Usage:
 *   middleware_chain_add(&chain, session_middleware_loader, store);
 */
MiddlewareResult session_middleware_loader(HttpRequest *req,
                                            HttpResponse *res,
                                            MiddlewareContext *ctx) {
    (void)res;
    if (!ctx || !ctx->user_data) return MIDDLEWARE_NEXT;

    SessionStore *store = (SessionStore *)ctx->user_data;
    char sid[SESSION_ID_LEN];

    if (session_parse_cookie(req, store->cookie_name, sid, sizeof(sid))) {
        Session *s = session_get(store, sid);
        if (s) {
            /* Store the session pointer in user_data for downstream use.
             * Overwrite user_data — callers should save original if needed. */
            ctx->user_data = s;
        }
    }
    return MIDDLEWARE_NEXT;
}
