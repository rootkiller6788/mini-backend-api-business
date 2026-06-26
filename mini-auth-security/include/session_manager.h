#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * session_manager.h -- Centralized Session Management
 *
 * Knowledge Layers:
 *   L1: Definitions -- session state, lifecycle enums, session storage
 *   L2: Core Concepts -- session fixation, idle timeout, absolute expiry
 *   L3: Engineering -- LRU eviction, session rotation, attribute storage
 *   L4: Standards -- OWASP Session Management Cheat Sheet, RFC 6265 (Cookies)
 *   L6: Canonical Problems -- distributed session management
 *   L7: Applications -- web application session lifecycle
 */

#define SM_MAX_SESSIONS          2048
#define SM_MAX_SESSION_ID_LEN    128
#define SM_MAX_USERNAME_LEN      128
#define SM_MAX_ATTR_KEY_LEN      64
#define SM_MAX_ATTR_VALUE_LEN    256
#define SM_MAX_ATTRIBUTES        16
#define SM_DEFAULT_IDLE_TIMEOUT  1800
#define SM_DEFAULT_ABSOLUTE_TTL  28800
#define SM_MAX_IP_LEN            64
#define SM_MAX_USER_AGENT_LEN    256

typedef enum {
    SESSION_ACTIVE = 0,
    SESSION_IDLE,
    SESSION_EXPIRED,
    SESSION_TERMINATED,
    SESSION_INVALIDATED
} SessionState;

typedef enum {
    SESSION_FIXATION_NONE = 0,
    SESSION_FIXATION_PREVENT,
    SESSION_FIXATION_REGENERATE
} SessionFixationPolicy;

typedef struct {
    char key[SM_MAX_ATTR_KEY_LEN];
    char value[SM_MAX_ATTR_VALUE_LEN];
} SessionAttribute;

typedef struct {
    char     session_id[SM_MAX_SESSION_ID_LEN];
    char     username[SM_MAX_USERNAME_LEN];
    char     client_ip[SM_MAX_IP_LEN];
    char     user_agent[SM_MAX_USER_AGENT_LEN];
    SessionState state;
    time_t   created_at;
    time_t   last_accessed_at;
    time_t   expires_at;
    uint32_t idle_timeout_seconds;
    uint32_t absolute_ttl_seconds;
    int      is_authenticated;
    int      privilege_level;
    SessionAttribute attributes[SM_MAX_ATTRIBUTES];
    size_t   attr_count;
    uint32_t access_count;
    int      rotation_generation;
} SessionEntry;

typedef struct {
    SessionEntry sessions[SM_MAX_SESSIONS];
    size_t       count;
    size_t       total_created;
    size_t       total_destroyed;
    SessionFixationPolicy fixation_policy;
    uint32_t      default_idle_timeout;
    uint32_t      default_absolute_ttl;
    int           enforce_ip_binding;
    int           enforce_user_agent_binding;
} SessionManager;

void sm_init(SessionManager *sm);

int sm_create_session(SessionManager *sm, const char *username,
                       const char *client_ip, const char *user_agent,
                       char *session_id_out, size_t sid_size);

int sm_validate_session(SessionManager *sm, const char *session_id,
                         const char *client_ip, const char *user_agent,
                         char *username_out, size_t u_size);

int sm_extend_session(SessionManager *sm, const char *session_id,
                       uint32_t additional_seconds);

int sm_terminate_session(SessionManager *sm, const char *session_id);

int sm_regenerate_session(SessionManager *sm, const char *old_session_id,
                           char *new_session_id_out, size_t sid_size);

int sm_set_session_attribute(SessionManager *sm, const char *session_id,
                              const char *key, const char *value);

int sm_get_session_attribute(const SessionManager *sm, const char *session_id,
                              const char *key, char *value_out, size_t v_size);

int sm_set_authenticated(SessionManager *sm, const char *session_id,
                          int privilege_level);

int sm_invalidate_all_for_user(SessionManager *sm, const char *username);

int sm_get_active_count(const SessionManager *sm);
int sm_get_idle_sessions(SessionManager *sm,
                          SessionEntry *results, size_t max_results,
                          size_t *count);

int sm_cleanup_expired(SessionManager *sm);

int sm_serialize_session(const SessionEntry *session,
                          char *json_out, size_t json_size);

void sm_dump_session(const SessionEntry *session);

const char *sm_session_state_name(SessionState state);

#endif /* SESSION_MANAGER_H */
