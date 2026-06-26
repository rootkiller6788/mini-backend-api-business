#include "session_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * L1: Session state name lookup
 * ======================================================================== */
const char *sm_session_state_name(SessionState state) {
    switch (state) {
    case SESSION_ACTIVE:      return "ACTIVE";
    case SESSION_IDLE:        return "IDLE";
    case SESSION_EXPIRED:     return "EXPIRED";
    case SESSION_TERMINATED:  return "TERMINATED";
    case SESSION_INVALIDATED: return "INVALIDATED";
    default: return "UNKNOWN";
    }
}

/* ========================================================================
 * L2: PRNG-based session ID generation
 *
 * Uses xorshift128+ algorithm for fast, statistically good random IDs.
 * Session IDs are 128-bit hex strings.
 *
 * Complexity: O(1) time
 * ======================================================================== */
static uint32_t sm_xorshift_state[4] = {0xDEADBEEF, 0xCAFEBABE, 0x8BADF00D, 0xFEEDFACE};

static uint64_t sm_xorshift128plus(void) {
    uint64_t s1 = ((uint64_t)sm_xorshift_state[1] << 32) | sm_xorshift_state[0];
    uint64_t s0 = ((uint64_t)sm_xorshift_state[3] << 32) | sm_xorshift_state[2];
    uint64_t result = s0 + s1;
    sm_xorshift_state[0] = (uint32_t)(s0 & 0xFFFFFFFF);
    sm_xorshift_state[1] = (uint32_t)(s0 >> 32);
    s1 ^= s1 << 23;
    sm_xorshift_state[2] = (uint32_t)((s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5)) & 0xFFFFFFFF);
    sm_xorshift_state[3] = (uint32_t)(((s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5)) >> 32) & 0xFFFFFFFF);
    return result;
}

static void sm_generate_session_id(char *buf, size_t len) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    uint64_t r;

    /* Seed with time if state is still default */
    if (sm_xorshift_state[0] == 0xDEADBEEF &&
        sm_xorshift_state[1] == 0xCAFEBABE) {
        sm_xorshift_state[0] = (uint32_t)time(NULL);
        sm_xorshift_state[1] = (uint32_t)((uint64_t)time(NULL) >> 32);
        sm_xorshift_state[2] = (uint32_t)(time(NULL) ^ 0x12345678);
        sm_xorshift_state[3] = 0x9ABCDEF0;
    }

    for (i = 0; i + 1 < len; i += 16) {
        r = sm_xorshift128plus();
        int j;
        for (j = 0; j < 16 && i + j + 1 < len; j++) {
            buf[i + j] = hex[(r >> (j * 4)) & 15];
        }
    }
    buf[len - 1] = '\0';
}

/* ========================================================================
 * L2: Initialize session manager with OWASP-recommended defaults
 *
 * OWASP Session Management Cheat Sheet:
 *   - Idle timeout: 15-30 minutes (we default to 30 min = 1800s)
 *   - Absolute timeout: 4-8 hours (we default to 8h = 28800s)
 *   - Session fixation protection: enabled
 * ======================================================================== */
void sm_init(SessionManager *sm) {
    if (!sm) return;
    memset(sm, 0, sizeof(*sm));
    sm->fixation_policy = SESSION_FIXATION_PREVENT;
    sm->default_idle_timeout = SM_DEFAULT_IDLE_TIMEOUT;
    sm->default_absolute_ttl = SM_DEFAULT_ABSOLUTE_TTL;
    sm->enforce_ip_binding = 0;
    sm->enforce_user_agent_binding = 0;
}

/* ========================================================================
 * L3: Create a new session
 *
 * Session lifecycle:
 *   1. Generate unique session ID (128-bit entropy)
 *   2. Set creation time, idle timeout, absolute expiry
 *   3. Bind to client IP and User-Agent (optional)
 *   4. Return session ID to caller
 *
 * OWASP: Session ID must be at least 128 bits of entropy and generated
 * by a CSPRNG. We use xorshift128+ (not cryptographically secure, but
 * good enough for non-adversarial use; production should use /dev/urandom).
 *
 * Complexity: O(1) time
 * ======================================================================== */
int sm_create_session(SessionManager *sm, const char *username,
                       const char *client_ip, const char *user_agent,
                       char *session_id_out, size_t sid_size) {
    size_t i;
    time_t now;

    if (!sm || !username || !session_id_out || sid_size == 0) return -1;
    if (sm->count >= SM_MAX_SESSIONS) return -1;

    now = time(NULL);
    i = sm->count;

    sm_generate_session_id(sm->sessions[i].session_id,
                           SM_MAX_SESSION_ID_LEN);

    strncpy(sm->sessions[i].username, username, SM_MAX_USERNAME_LEN - 1);

    if (client_ip)
        strncpy(sm->sessions[i].client_ip, client_ip, SM_MAX_IP_LEN - 1);
    if (user_agent)
        strncpy(sm->sessions[i].user_agent, user_agent, SM_MAX_USER_AGENT_LEN - 1);

    sm->sessions[i].state = SESSION_ACTIVE;
    sm->sessions[i].created_at = now;
    sm->sessions[i].last_accessed_at = now;
    sm->sessions[i].expires_at = now + sm->default_absolute_ttl;
    sm->sessions[i].idle_timeout_seconds = sm->default_idle_timeout;
    sm->sessions[i].absolute_ttl_seconds = sm->default_absolute_ttl;
    sm->sessions[i].is_authenticated = 0;
    sm->sessions[i].privilege_level = 0;
    sm->sessions[i].attr_count = 0;
    sm->sessions[i].access_count = 0;
    sm->sessions[i].rotation_generation = 1;

    sm->count++;
    sm->total_created++;

    if (session_id_out && sid_size > 0) {
        strncpy(session_id_out, sm->sessions[i].session_id, sid_size - 1);
        session_id_out[sid_size - 1] = '\0';
    }
    return 0;
}

/* ========================================================================
 * L2: Find session by ID (internal helper)
 *
 * Linear scan through session array. For production, use hash table
 * or binary search on sorted IDs.
 *
 * Complexity: O(n) time
 * ======================================================================== */
static SessionEntry *sm_find_session(SessionManager *sm, const char *session_id) {
    size_t i;
    if (!sm || !session_id) return NULL;
    for (i = 0; i < sm->count; i++) {
        if (strcmp(sm->sessions[i].session_id, session_id) == 0) {
            return &sm->sessions[i];
        }
    }
    return NULL;
}

/* ========================================================================
 * L6: Validate session (canonical web app problem)
 *
 * Checks:
 *   1. Session exists
 *   2. Session not expired (absolute TTL)
 *   3. Session not idle-timed-out
 *   4. Optional IP binding match
 *   5. Optional User-Agent binding match
 *
 * On successful validation, updates last_accessed_at and access_count.
 * On expiry, sets session state accordingly.
 *
 * OWASP: Session validation must happen on every authenticated request.
 *
 * Complexity: O(n) time (find session)
 * ======================================================================== */
int sm_validate_session(SessionManager *sm, const char *session_id,
                         const char *client_ip, const char *user_agent,
                         char *username_out, size_t u_size) {
    SessionEntry *s;
    time_t now;

    if (!sm || !session_id) return -1;
    now = time(NULL);

    s = sm_find_session(sm, session_id);
    if (!s) return -2; /* Not found */

    if (s->state == SESSION_TERMINATED || s->state == SESSION_INVALIDATED)
        return -3;

    /* Check absolute expiry */
    if (now > s->expires_at) {
        s->state = SESSION_EXPIRED;
        return -4;
    }

    /* Check idle timeout */
    if (now - s->last_accessed_at > (time_t)s->idle_timeout_seconds) {
        s->state = SESSION_IDLE;
        return -5;
    }

    /* Check IP binding (if enforced) */
    if (sm->enforce_ip_binding && client_ip && s->client_ip[0] != '\0') {
        if (strcmp(s->client_ip, client_ip) != 0) return -6;
    }

    /* Check User-Agent binding (if enforced) */
    if (sm->enforce_user_agent_binding && user_agent &&
        s->user_agent[0] != '\0') {
        if (strcmp(s->user_agent, user_agent) != 0) return -7;
    }

    /* Update session state */
    s->last_accessed_at = now;
    s->access_count++;
    if (s->state == SESSION_IDLE) s->state = SESSION_ACTIVE;

    if (username_out && u_size > 0) {
        strncpy(username_out, s->username, u_size - 1);
        username_out[u_size - 1] = '\0';
    }
    return 0;
}

/* ========================================================================
 * L3: Extend session by additional seconds
 *
 * Updates both idle timeout reference (last_accessed_at) and absolute
 * expiry if the extension pushes beyond original absolute TTL.
 *
 * Complexity: O(n) time
 * ======================================================================== */
int sm_extend_session(SessionManager *sm, const char *session_id,
                       uint32_t additional_seconds) {
    SessionEntry *s;
    time_t now;

    if (!sm || !session_id) return -1;
    now = time(NULL);

    s = sm_find_session(sm, session_id);
    if (!s) return -2;
    if (s->state == SESSION_TERMINATED || s->state == SESSION_INVALIDATED)
        return -3;

    s->last_accessed_at = now;
    s->expires_at += additional_seconds;
    if (s->state == SESSION_IDLE) s->state = SESSION_ACTIVE;

    return 0;
}

/* ========================================================================
 * L2: Terminate a session (logout)
 *
 * Sets state to TERMINATED. Session ID won't validate again.
 * Data preserved for audit trail.
 *
 * Complexity: O(n) time
 * ======================================================================== */
int sm_terminate_session(SessionManager *sm, const char *session_id) {
    SessionEntry *s;
    if (!sm || !session_id) return -1;

    s = sm_find_session(sm, session_id);
    if (!s) return -2;

    s->state = SESSION_TERMINATED;
    sm->total_destroyed++;
    return 0;
}

/* ========================================================================
 * L3: Regenerate session ID (session fixation prevention)
 *
 * OWASP: Regenerate session ID after privilege level change.
 * This prevents session fixation attacks where attacker sets victim's
 * session ID and later hijacks the authenticated session.
 *
 * Algorithm:
 *   1. Create new session copying attributes from old
 *   2. Mark old session as INVALIDATED
 *   3. Return new session ID
 *
 * Complexity: O(n) time
 * ======================================================================== */
int sm_regenerate_session(SessionManager *sm, const char *old_session_id,
                           char *new_session_id_out, size_t sid_size) {
    SessionEntry *old, *new_s;
    size_t i;

    if (!sm || !old_session_id || !new_session_id_out || sid_size == 0)
        return -1;

    old = sm_find_session(sm, old_session_id);
    if (!old) return -2;
    if (sm->count >= SM_MAX_SESSIONS) return -3;

    i = sm->count;
    new_s = &sm->sessions[i];

    /* Copy old session data */
    memcpy(new_s, old, sizeof(SessionEntry));

    /* Generate new ID and increment generation */
    sm_generate_session_id(new_s->session_id, SM_MAX_SESSION_ID_LEN);
    new_s->rotation_generation++;

    /* Invalidate old session */
    old->state = SESSION_INVALIDATED;

    sm->count++;

    strncpy(new_session_id_out, new_s->session_id, sid_size - 1);
    new_session_id_out[sid_size - 1] = '\0';
    return 0;
}

/* ========================================================================
 * L7: Set session attribute (key-value store on session)
 *
 * Used for: shopping cart ID, CSRF token, preferred language,
 *           last-viewed page, OAuth2 state parameter, etc.
 *
 * Complexity: O(n) to find session + O(k) for attributes
 * ======================================================================== */
int sm_set_session_attribute(SessionManager *sm, const char *session_id,
                              const char *key, const char *value) {
    SessionEntry *s;
    size_t i;

    if (!sm || !session_id || !key || !value) return -1;

    s = sm_find_session(sm, session_id);
    if (!s) return -2;

    /* Update existing attribute if key matches */
    for (i = 0; i < s->attr_count; i++) {
        if (strcmp(s->attributes[i].key, key) == 0) {
            strncpy(s->attributes[i].value, value, SM_MAX_ATTR_VALUE_LEN - 1);
            return 0;
        }
    }

    /* Add new attribute */
    if (s->attr_count >= SM_MAX_ATTRIBUTES) return -3;

    i = s->attr_count;
    strncpy(s->attributes[i].key, key, SM_MAX_ATTR_KEY_LEN - 1);
    strncpy(s->attributes[i].value, value, SM_MAX_ATTR_VALUE_LEN - 1);
    s->attr_count++;
    return 0;
}

/* ========================================================================
 * L7: Get session attribute
 *
 * Complexity: O(n) time
 * ======================================================================== */
int sm_get_session_attribute(const SessionManager *sm, const char *session_id,
                              const char *key, char *value_out, size_t v_size) {
    const SessionEntry *s;
    size_t i;

    if (!sm || !session_id || !key || !value_out || v_size == 0) return -1;

    s = sm_find_session((SessionManager *)sm, session_id);
    if (!s) return -2;

    for (i = 0; i < s->attr_count; i++) {
        if (strcmp(s->attributes[i].key, key) == 0) {
            strncpy(value_out, s->attributes[i].value, v_size - 1);
            value_out[v_size - 1] = '\0';
            return 0;
        }
    }
    return -3; /* Not found */
}

/* ========================================================================
 * L3: Mark session as authenticated with privilege level
 *
 * Should trigger session ID regeneration for fixation prevention.
 * Privilege levels: 0 = anonymous, 1 = user, 10 = admin
 *
 * Complexity: O(n) time
 * ======================================================================== */
int sm_set_authenticated(SessionManager *sm, const char *session_id,
                          int privilege_level) {
    SessionEntry *s;

    if (!sm || !session_id) return -1;

    s = sm_find_session(sm, session_id);
    if (!s) return -2;

    s->is_authenticated = 1;
    s->privilege_level = privilege_level;
    return 0;
}

/* ========================================================================
 * L7: Invalidate all sessions for a user (force logout everywhere)
 *
 * Used for: password change, account lockout, security incident response
 *
 * Complexity: O(n) time
 * ======================================================================== */
int sm_invalidate_all_for_user(SessionManager *sm, const char *username) {
    size_t i;
    int count = 0;

    if (!sm || !username) return -1;

    for (i = 0; i < sm->count; i++) {
        if (strcmp(sm->sessions[i].username, username) == 0 &&
            sm->sessions[i].state == SESSION_ACTIVE) {
            sm->sessions[i].state = SESSION_INVALIDATED;
            sm->total_destroyed++;
            count++;
        }
    }
    return count;
}

/* ========================================================================
 * L7: Get count of active sessions
 *
 * Complexity: O(n) time
 * ======================================================================== */
int sm_get_active_count(const SessionManager *sm) {
    size_t i;
    int count = 0;

    if (!sm) return 0;

    for (i = 0; i < sm->count; i++) {
        if (sm->sessions[i].state == SESSION_ACTIVE) count++;
    }
    return count;
}

/* ========================================================================
 * L7: Get idle sessions (warning: near timeout)
 *
 * Returns sessions that haven't been accessed in > half the idle timeout.
 * Used for proactive session cleanup and user notification.
 *
 * Complexity: O(n) time
 * ======================================================================== */
int sm_get_idle_sessions(SessionManager *sm,
                          SessionEntry *results, size_t max_results,
                          size_t *count) {
    size_t i;
    time_t now = time(NULL);
    *count = 0;

    if (!sm || !results || max_results == 0) return -1;

    for (i = 0; i < sm->count && *count < max_results; i++) {
        if (sm->sessions[i].state == SESSION_ACTIVE) {
            time_t idle_time = now - sm->sessions[i].last_accessed_at;
            if (idle_time > (time_t)(sm->sessions[i].idle_timeout_seconds / 2)) {
                memcpy(&results[*count], &sm->sessions[i], sizeof(SessionEntry));
                (*count)++;
            }
        }
    }
    return 0;
}

/* ========================================================================
 * L6: Cleanup expired sessions
 *
 * Removes EXPIRED, TERMINATED, and INVALIDATED sessions from storage.
 * Compacts array by swapping with last element (O(1) removal).
 *
 * Complexity: O(n) time, O(1) space
 * ======================================================================== */
int sm_cleanup_expired(SessionManager *sm) {
    size_t i;
    time_t now = time(NULL);

    if (!sm) return -1;

    for (i = 0; i < sm->count; ) {
        SessionEntry *s = &sm->sessions[i];
        int should_remove = 0;

        if (s->state == SESSION_TERMINATED ||
            s->state == SESSION_INVALIDATED) {
            should_remove = 1;
        } else if (s->state == SESSION_EXPIRED ||
                   now > s->expires_at) {
            should_remove = 1;
        } else if (now - s->last_accessed_at >
                   (time_t)s->idle_timeout_seconds * 2) {
            /* Double idle timeout = definitely stale */
            should_remove = 1;
        }

        if (should_remove) {
            sm->total_destroyed++;
            if (i < sm->count - 1) {
                sm->sessions[i] = sm->sessions[sm->count - 1];
            }
            sm->count--;
        } else {
            i++;
        }
    }
    return 0;
}

/* ========================================================================
 * L7: Serialize session to JSON (for logging / debugging)
 *
 * Complexity: O(1) per session
 * ======================================================================== */
int sm_serialize_session(const SessionEntry *session,
                          char *json_out, size_t json_size) {
    int len;
    if (!session || !json_out || json_size == 0) return -1;

    len = snprintf(json_out, json_size,
             "{\"session_id\":\"%s\",\"username\":\"%s\","
             "\"state\":\"%s\",\"authenticated\":%d,"
             "\"privilege\":%d,\"access_count\":%u,"
             "\"created\":%lld,\"last_access\":%lld,"
             "\"expires\":%lld,\"generation\":%d}",
             session->session_id, session->username,
             sm_session_state_name(session->state),
             session->is_authenticated, session->privilege_level,
             session->access_count,
             (long long)session->created_at,
             (long long)session->last_accessed_at,
             (long long)session->expires_at,
             session->rotation_generation);
    return len;
}

/* ========================================================================
 * L1: Dump session to stdout (debug)
 * ======================================================================== */
void sm_dump_session(const SessionEntry *s) {
    char json[2048];
    if (!s) return;
    sm_serialize_session(s, json, sizeof(json));
    printf("%s\n", json);
}