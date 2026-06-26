#ifndef AUDIT_LOG_H
#define AUDIT_LOG_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * audit_log.h -- Security Audit Logging Framework
 *
 * Knowledge Layers:
 *   L1: Definitions -- event types, severity, audit record
 *   L2: Core Concepts -- non-repudiation, tamper evidence, chain of custody
 *   L3: Engineering -- hash-chain integrity (Merkle-Damgard style linking)
 *   L4: Standards -- NIST SP 800-92 (Log Management), ISO 27001 A.12.4
 *   L7: Applications -- security incident detection, compliance reporting
 *   L8: Advanced -- tamper-detection via hash chain, immutable audit trail
 */

#define AUDIT_MAX_EVENTS          4096
#define AUDIT_MAX_MESSAGE_LEN     512
#define AUDIT_MAX_SUBJECT_LEN     128
#define AUDIT_MAX_OBJECT_LEN      256
#define AUDIT_MAX_SOURCE_IP_LEN   64
#define AUDIT_MAX_HASH_LEN        64
#define AUDIT_MAX_RECORD_BUF_LEN  2048

typedef enum {
    AUDIT_SEVERITY_DEBUG = 0,
    AUDIT_SEVERITY_INFO,
    AUDIT_SEVERITY_WARNING,
    AUDIT_SEVERITY_ERROR,
    AUDIT_SEVERITY_CRITICAL
} AuditSeverity;

typedef enum {
    AUDIT_EVENT_LOGIN_SUCCESS = 0,
    AUDIT_EVENT_LOGIN_FAILURE,
    AUDIT_EVENT_LOGOUT,
    AUDIT_EVENT_ACCESS_GRANTED,
    AUDIT_EVENT_ACCESS_DENIED,
    AUDIT_EVENT_PERMISSION_CHANGE,
    AUDIT_EVENT_ROLE_ASSIGNMENT,
    AUDIT_EVENT_TOKEN_CREATED,
    AUDIT_EVENT_TOKEN_REVOKED,
    AUDIT_EVENT_SESSION_CREATED,
    AUDIT_EVENT_SESSION_EXPIRED,
    AUDIT_EVENT_CONFIG_CHANGE,
    AUDIT_EVENT_RATE_LIMIT_HIT,
    AUDIT_EVENT_CSRF_VALIDATION,
    AUDIT_EVENT_XSS_DETECTED,
    AUDIT_EVENT_CUSTOM
} AuditEventType;

typedef struct {
    uint64_t    event_id;
    AuditEventType event_type;
    AuditSeverity severity;
    char        subject[AUDIT_MAX_SUBJECT_LEN];
    char        object[AUDIT_MAX_OBJECT_LEN];
    char        action[128];
    char        message[AUDIT_MAX_MESSAGE_LEN];
    char        source_ip[AUDIT_MAX_SOURCE_IP_LEN];
    char        prev_hash_hex[AUDIT_MAX_HASH_LEN];
    char        curr_hash_hex[AUDIT_MAX_HASH_LEN];
    time_t      timestamp;
    int         outcome;
    uint32_t    record_crc;
} AuditRecord;

typedef struct {
    AuditRecord  records[AUDIT_MAX_EVENTS];
    size_t       count;
    size_t       total_events;
    uint64_t     next_event_id;
    char         genesis_hash[AUDIT_MAX_HASH_LEN];
    int          tampered;
    int          integrity_enabled;
} AuditLog;

typedef struct {
    AuditEventType event_type;
    size_t         count;
    time_t         first_seen;
    time_t         last_seen;
} AuditEventStats;

void audit_log_init(AuditLog *log, int enable_integrity);

int audit_log_event(AuditLog *log, AuditEventType event_type,
                    AuditSeverity severity,
                    const char *subject, const char *object,
                    const char *action, const char *source_ip,
                    int outcome, const char *message);

int audit_verify_integrity(AuditLog *log);

int audit_query_by_subject(const AuditLog *log, const char *subject,
                           AuditRecord *results, size_t max_results,
                           size_t *result_count);

int audit_query_by_type(const AuditLog *log, AuditEventType event_type,
                         AuditRecord *results, size_t max_results,
                         size_t *result_count);

int audit_query_by_timerange(const AuditLog *log, time_t start, time_t end,
                              AuditRecord *results, size_t max_results,
                              size_t *result_count);

int audit_get_stats(const AuditLog *log, AuditEventType event_type,
                     AuditEventStats *stats);

int audit_export_json(const AuditLog *log,
                       AuditRecord *records, size_t count,
                       char *json_out, size_t json_size);

const char *audit_severity_name(AuditSeverity sev);
const char *audit_event_name(AuditEventType evt);

#endif /* AUDIT_LOG_H */
