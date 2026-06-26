#include "audit_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * L1: Severity name lookup
 * ======================================================================== */
const char *audit_severity_name(AuditSeverity sev) {
    switch (sev) {
    case AUDIT_SEVERITY_DEBUG:    return "DEBUG";
    case AUDIT_SEVERITY_INFO:     return "INFO";
    case AUDIT_SEVERITY_WARNING:  return "WARNING";
    case AUDIT_SEVERITY_ERROR:    return "ERROR";
    case AUDIT_SEVERITY_CRITICAL: return "CRITICAL";
    default: return "UNKNOWN";
    }
}

/* ========================================================================
 * L1: Event type name lookup
 * ======================================================================== */
const char *audit_event_name(AuditEventType evt) {
    switch (evt) {
    case AUDIT_EVENT_LOGIN_SUCCESS:     return "LOGIN_SUCCESS";
    case AUDIT_EVENT_LOGIN_FAILURE:     return "LOGIN_FAILURE";
    case AUDIT_EVENT_LOGOUT:            return "LOGOUT";
    case AUDIT_EVENT_ACCESS_GRANTED:    return "ACCESS_GRANTED";
    case AUDIT_EVENT_ACCESS_DENIED:     return "ACCESS_DENIED";
    case AUDIT_EVENT_PERMISSION_CHANGE: return "PERMISSION_CHANGE";
    case AUDIT_EVENT_ROLE_ASSIGNMENT:   return "ROLE_ASSIGNMENT";
    case AUDIT_EVENT_TOKEN_CREATED:     return "TOKEN_CREATED";
    case AUDIT_EVENT_TOKEN_REVOKED:     return "TOKEN_REVOKED";
    case AUDIT_EVENT_SESSION_CREATED:   return "SESSION_CREATED";
    case AUDIT_EVENT_SESSION_EXPIRED:   return "SESSION_EXPIRED";
    case AUDIT_EVENT_CONFIG_CHANGE:     return "CONFIG_CHANGE";
    case AUDIT_EVENT_RATE_LIMIT_HIT:    return "RATE_LIMIT_HIT";
    case AUDIT_EVENT_CSRF_VALIDATION:   return "CSRF_VALIDATION";
    case AUDIT_EVENT_XSS_DETECTED:      return "XSS_DETECTED";
    case AUDIT_EVENT_CUSTOM:            return "CUSTOM";
    default: return "UNKNOWN";
    }
}

/* ========================================================================
 * L2: FNV-1a 64-bit hash for integrity chain linking
 *
 * FNV_offset_basis = 14695981039346656037ULL
 * FNV_prime = 1099511628211ULL
 *
 * Each record links to previous via hash, forming append-only chain.
 * If any record is modified, all subsequent hash links break.
 * ======================================================================== */
static uint64_t audit_fnv1a_64(const uint8_t *data, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    size_t i;
    for (i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* ========================================================================
 * L2: Compute integrity hash for a record
 * ======================================================================== */
static void audit_compute_record_hash(const AuditRecord *rec,
                                       char hash_hex_out[AUDIT_MAX_HASH_LEN]) {
    uint8_t buf[AUDIT_MAX_RECORD_BUF_LEN];
    int len;
    uint64_t hash;

    len = snprintf((char *)buf, sizeof(buf),
                   "%llu|%d|%d|%s|%s|%s|%s|%s|%lld|%d",
                   (unsigned long long)rec->event_id,
                   (int)rec->event_type, (int)rec->severity,
                   rec->subject, rec->object, rec->action,
                   rec->message, rec->source_ip,
                   (long long)rec->timestamp, rec->outcome);

    hash = audit_fnv1a_64(buf, (size_t)len);
    snprintf(hash_hex_out, AUDIT_MAX_HASH_LEN, "%016llx",
             (unsigned long long)hash);
}

/* ========================================================================
 * L2: Initialize audit log with integrity hashing
 *
 * Genesis hash starts the chain. All subsequent records link to it.
 * ======================================================================== */
void audit_log_init(AuditLog *log, int enable_integrity) {
    if (!log) return;
    memset(log, 0, sizeof(*log));
    log->integrity_enabled = enable_integrity;
    log->tampered = 0;
    log->next_event_id = 1;
    snprintf(log->genesis_hash, sizeof(log->genesis_hash),
             "0000000000000000");
}

/* ========================================================================
 * L3: Append event to audit log with hash-chain linking
 *
 * NIST SP 800-92 Log Management:
 *   1. Unique sequential event ID
 *   2. UTC timestamp
 *   3. Subject/Object/Action triple
 *   4. Outcome (success/failure)
 *
 * Hash chain: Record_i.prev_hash = H(Record_{i-1}.data)
 * ======================================================================== */
int audit_log_event(AuditLog *log, AuditEventType event_type,
                    AuditSeverity severity,
                    const char *subject, const char *object,
                    const char *action, const char *source_ip,
                    int outcome, const char *message) {
    size_t i;
    if (!log || log->count >= AUDIT_MAX_EVENTS) return -1;

    i = log->count;
    log->records[i].event_id = log->next_event_id++;
    log->records[i].event_type = event_type;
    log->records[i].severity = severity;
    log->records[i].timestamp = time(NULL);
    log->records[i].outcome = outcome;

    if (subject)    strncpy(log->records[i].subject, subject,
                            AUDIT_MAX_SUBJECT_LEN - 1);
    if (object)     strncpy(log->records[i].object, object,
                            AUDIT_MAX_OBJECT_LEN - 1);
    if (action)     strncpy(log->records[i].action, action, 127);
    if (source_ip)  strncpy(log->records[i].source_ip, source_ip,
                            AUDIT_MAX_SOURCE_IP_LEN - 1);
    if (message)    strncpy(log->records[i].message, message,
                            AUDIT_MAX_MESSAGE_LEN - 1);

    /* Build hash chain links */
    if (log->integrity_enabled) {
        if (i == 0) {
            strncpy(log->records[i].prev_hash_hex, log->genesis_hash,
                    AUDIT_MAX_HASH_LEN - 1);
        } else {
            strncpy(log->records[i].prev_hash_hex,
                    log->records[i - 1].curr_hash_hex,
                    AUDIT_MAX_HASH_LEN - 1);
        }
        audit_compute_record_hash(&log->records[i],
                                  log->records[i].curr_hash_hex);
    } else {
        log->records[i].prev_hash_hex[0] = '\0';
        log->records[i].curr_hash_hex[0] = '\0';
    }

    /* CRC-32 for quick corruption detection */
    {
        const uint8_t *raw = (const uint8_t *)&log->records[i];
        uint32_t crc = 0xFFFFFFFF;
        size_t k;
        int b;
        for (k = 0; k < sizeof(AuditRecord) - sizeof(uint32_t); k++) {
            crc ^= (uint32_t)raw[k];
            for (b = 0; b < 8; b++) {
                if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
                else crc >>= 1;
            }
        }
        log->records[i].record_crc = ~crc;
    }

    log->count++;
    log->total_events++;
    return 0;
}

/* ========================================================================
 * L8: Verify hash-chain integrity of entire audit log
 *
 * Tamper detection via hash chain (simplified Merkle-Damgard):
 *   For each record i:
 *     a. Recompute hash -> compare with stored curr_hash
 *     b. Verify prev_hash links to record[i-1].curr_hash
 *
 * Any modification to record k breaks all hashes k..n.
 *
 * Complexity: O(n) time, O(1) space
 * ======================================================================== */
int audit_verify_integrity(AuditLog *log) {
    size_t i;
    char computed_hash[AUDIT_MAX_HASH_LEN];
    char expected_prev[AUDIT_MAX_HASH_LEN];

    if (!log) return -1;
    if (!log->integrity_enabled) return 0;

    log->tampered = 0;

    for (i = 0; i < log->count; i++) {
        audit_compute_record_hash(&log->records[i], computed_hash);

        if (strcmp(log->records[i].curr_hash_hex, computed_hash) != 0) {
            log->tampered = 1;
            return -1;
        }

        if (i == 0) {
            strncpy(expected_prev, log->genesis_hash, AUDIT_MAX_HASH_LEN - 1);
        } else {
            strncpy(expected_prev, log->records[i - 1].curr_hash_hex,
                    AUDIT_MAX_HASH_LEN - 1);
        }

        if (strcmp(log->records[i].prev_hash_hex, expected_prev) != 0) {
            log->tampered = 1;
            return -2;
        }
    }
    return 0;
}

/* ========================================================================
 * L7: Query audit records by subject (who performed the action)
 *
 * Used for: user activity reports, compliance audits, forensics
 * Complexity: O(n) time
 * ======================================================================== */
int audit_query_by_subject(const AuditLog *log, const char *subject,
                           AuditRecord *results, size_t max_results,
                           size_t *result_count) {
    size_t i;
    *result_count = 0;
    if (!log || !subject || !results || max_results == 0) return -1;

    for (i = 0; i < log->count && *result_count < max_results; i++) {
        if (strcmp(log->records[i].subject, subject) == 0) {
            memcpy(&results[*result_count], &log->records[i],
                   sizeof(AuditRecord));
            (*result_count)++;
        }
    }
    return 0;
}

/* ========================================================================
 * L7: Query audit records by event type
 *
 * Used for: pattern analysis, anomaly detection
 * Complexity: O(n) time
 * ======================================================================== */
int audit_query_by_type(const AuditLog *log, AuditEventType event_type,
                         AuditRecord *results, size_t max_results,
                         size_t *result_count) {
    size_t i;
    *result_count = 0;
    if (!log || !results || max_results == 0) return -1;

    for (i = 0; i < log->count && *result_count < max_results; i++) {
        if (log->records[i].event_type == event_type) {
            memcpy(&results[*result_count], &log->records[i],
                   sizeof(AuditRecord));
            (*result_count)++;
        }
    }
    return 0;
}

/* ========================================================================
 * L7: Query audit records by time range [start, end]
 *
 * Used for: incident response, compliance reporting
 * Complexity: O(n) time
 * ======================================================================== */
int audit_query_by_timerange(const AuditLog *log, time_t start, time_t end,
                              AuditRecord *results, size_t max_results,
                              size_t *result_count) {
    size_t i;
    *result_count = 0;
    if (!log || !results || max_results == 0 || start > end) return -1;

    for (i = 0; i < log->count && *result_count < max_results; i++) {
        if (log->records[i].timestamp >= start &&
            log->records[i].timestamp <= end) {
            memcpy(&results[*result_count], &log->records[i],
                   sizeof(AuditRecord));
            (*result_count)++;
        }
    }
    return 0;
}

/* ========================================================================
 * L7: Get statistics for an event type
 *
 * Tracks: count, first_seen, last_seen.
 * Used for: dashboards, alert thresholds, trend analysis
 * Complexity: O(n) time
 * ======================================================================== */
int audit_get_stats(const AuditLog *log, AuditEventType event_type,
                     AuditEventStats *stats) {
    size_t i;
    if (!log || !stats) return -1;

    memset(stats, 0, sizeof(*stats));
    stats->event_type = event_type;

    for (i = 0; i < log->count; i++) {
        if (log->records[i].event_type == event_type) {
            if (stats->count == 0) {
                stats->first_seen = log->records[i].timestamp;
            }
            stats->count++;
            stats->last_seen = log->records[i].timestamp;
        }
    }
    return 0;
}

/* ========================================================================
 * L7: Export audit records as JSON array
 *
 * Format: [{"event_id":1,"type":"LOGIN_SUCCESS",...}, ...]
 * Used for: SIEM integration, log shipping to ELK/Splunk
 * Complexity: O(n) time
 * ======================================================================== */
int audit_export_json(const AuditLog *log,
                       AuditRecord *records, size_t count,
                       char *json_out, size_t json_size) {
    size_t i;
    int offset = 0;
    int written;

    if (!log || !records || !json_out || json_size == 0) return -1;

    written = snprintf(json_out, json_size, "[");
    if (written < 0) return -1;
    offset = written;

    for (i = 0; i < count && (size_t)offset < json_size; i++) {
        char ts_buf[32];
        strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ",
                 gmtime(&records[i].timestamp));

        written = snprintf(json_out + offset, json_size - (size_t)offset,
                 "%s{\"event_id\":%llu,\"type\":\"%s\",\"severity\":\"%s\","
                 "\"subject\":\"%s\",\"object\":\"%s\",\"action\":\"%s\","
                 "\"outcome\":%d,\"timestamp\":\"%s\",\"message\":\"%s\"}",
                 (i > 0) ? "," : "",
                 (unsigned long long)records[i].event_id,
                 audit_event_name(records[i].event_type),
                 audit_severity_name(records[i].severity),
                 records[i].subject, records[i].object, records[i].action,
                 records[i].outcome, ts_buf, records[i].message);
        if (written < 0) break;
        offset += written;
    }

    if ((size_t)offset + 2 < json_size) {
        json_out[offset++] = ']';
        json_out[offset] = '\0';
    }
    return offset;
}