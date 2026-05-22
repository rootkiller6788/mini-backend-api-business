#include "grpc_sim.h"
#include <string.h>
#include <stdio.h>

const char* grpc_rpc_type_string(grpc_rpc_type_t t) {
    static const char* names[] = { "UNARY", "SERVER_STREAM", "CLIENT_STREAM", "BIDI_STREAM" };
    if (t > GRPC_BIDI_STREAM) return "UNKNOWN";
    return names[t];
}

const char* grpc_status_string(grpc_status_code_t c) {
    switch (c) {
        case GRPC_OK:                return "OK";
        case GRPC_CANCELLED:         return "CANCELLED";
        case GRPC_UNKNOWN:           return "UNKNOWN";
        case GRPC_INVALID_ARGUMENT:  return "INVALID_ARGUMENT";
        case GRPC_DEADLINE_EXCEEDED: return "DEADLINE_EXCEEDED";
        case GRPC_NOT_FOUND:         return "NOT_FOUND";
        case GRPC_ALREADY_EXISTS:    return "ALREADY_EXISTS";
        case GRPC_PERMISSION_DENIED: return "PERMISSION_DENIED";
        case GRPC_RESOURCE_EXHAUSTED:return "RESOURCE_EXHAUSTED";
        case GRPC_FAILED_PRECONDITION:return "FAILED_PRECONDITION";
        case GRPC_ABORTED:           return "ABORTED";
        case GRPC_OUT_OF_RANGE:      return "OUT_OF_RANGE";
        case GRPC_UNIMPLEMENTED:     return "UNIMPLEMENTED";
        case GRPC_INTERNAL:          return "INTERNAL";
        case GRPC_UNAVAILABLE:       return "UNAVAILABLE";
        case GRPC_DATA_LOSS:         return "DATA_LOSS";
        case GRPC_UNAUTHENTICATED:   return "UNAUTHENTICATED";
        default:                     return "UNKNOWN_CODE";
    }
}

const char* grpc_proto_type_string(grpc_proto_type_t t) {
    switch (t) {
        case GRPC_PROTO_DOUBLE:   return "double";
        case GRPC_PROTO_FLOAT:    return "float";
        case GRPC_PROTO_INT64:    return "int64";
        case GRPC_PROTO_UINT64:   return "uint64";
        case GRPC_PROTO_INT32:    return "int32";
        case GRPC_PROTO_FIXED64:  return "fixed64";
        case GRPC_PROTO_FIXED32:  return "fixed32";
        case GRPC_PROTO_BOOL:     return "bool";
        case GRPC_PROTO_STRING:   return "string";
        case GRPC_PROTO_BYTES:    return "bytes";
        case GRPC_PROTO_UINT32:   return "uint32";
        case GRPC_PROTO_ENUM:     return "enum";
        case GRPC_PROTO_SFIXED32: return "sfixed32";
        case GRPC_PROTO_SFIXED64: return "sfixed64";
        case GRPC_PROTO_SINT32:   return "sint32";
        case GRPC_PROTO_SINT64:   return "sint64";
        case GRPC_PROTO_MESSAGE:  return "message";
        default:                  return "unknown";
    }
}

const char* grpc_h2_frame_type_string(grpc_h2_frame_type_t t) {
    static const char* names[] = {
        "DATA", "HEADERS", "PRIORITY", "RST_STREAM", "SETTINGS",
        "PUSH_PROMISE", "PING", "GOAWAY", "WINDOW_UPDATE", "CONTINUATION"
    };
    if (t > GRPC_FRAME_CONTINUATION) return "UNKNOWN";
    return names[t];
}

void grpc_sim_init(grpc_sim_t* g) {
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->next_stream_id = 1;
}

grpc_service_def_t* grpc_sim_add_service(grpc_sim_t* g, const char* name, const char* package) {
    if (!g || g->service_count >= GRPC_MAX_SERVICES) return NULL;
    grpc_service_def_t* svc = &g->proto.services[g->service_count++];
    memset(svc, 0, sizeof(*svc));
    strncpy(svc->name, name, sizeof(svc->name) - 1);
    strncpy(svc->package, package, sizeof(svc->package) - 1);
    return svc;
}

grpc_method_def_t* grpc_sim_add_method(grpc_service_def_t* svc, const char* name, grpc_rpc_type_t rpc_type,
                                       const char* req_type, const char* resp_type) {
    if (!svc || svc->method_count >= GRPC_MAX_METHODS) return NULL;
    grpc_method_def_t* m = &svc->methods[svc->method_count++];
    memset(m, 0, sizeof(*m));
    strncpy(m->name, name, sizeof(m->name) - 1);
    m->rpc_type = rpc_type;
    strncpy(m->request_type, req_type, sizeof(m->request_type) - 1);
    strncpy(m->response_type, resp_type, sizeof(m->response_type) - 1);
    return m;
}

grpc_message_def_t* grpc_sim_add_message(grpc_sim_t* g, const char* name) {
    if (!g || g->proto.message_count >= (GRPC_MAX_SERVICES * GRPC_MAX_METHODS * 2)) return NULL;
    grpc_message_def_t* msg = &g->proto.messages[g->proto.message_count++];
    memset(msg, 0, sizeof(*msg));
    strncpy(msg->name, name, sizeof(msg->name) - 1);
    return msg;
}

void grpc_message_add_field(grpc_message_def_t* msg, const char* name, grpc_proto_type_t type,
                            int32_t field_number, bool repeated, bool optional) {
    if (!msg || msg->field_count >= GRPC_MAX_FIELDS) return;
    grpc_field_def_t* f = &msg->fields[msg->field_count++];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, sizeof(f->name) - 1);
    f->type = type;
    f->field_number = field_number;
    f->is_repeated = repeated;
    f->is_optional = optional;
}

char* grpc_sim_export_proto(grpc_sim_t* g, char* buf, size_t len) {
    if (!g || !buf) return NULL;
    int off = snprintf(buf, len,
        "// Auto-generated proto3 file\n"
        "syntax = \"proto3\";\n\n");
    for (int32_t i = 0; i < g->proto.service_count; i++) {
        grpc_service_def_t* svc = &g->proto.services[i];
        if (strlen(svc->package) > 0)
            off += snprintf(buf + off, len - off, "package %s;\n\n", svc->package);
        for (int32_t j = 0; j < g->proto.message_count; j++) {
            grpc_message_def_t* msg = &g->proto.messages[j];
            off += snprintf(buf + off, len - off, "message %s {\n", msg->name);
            for (int32_t k = 0; k < msg->field_count; k++) {
                grpc_field_def_t* f = &msg->fields[k];
                const char* prefix = f->is_repeated ? "repeated " : (f->is_optional ? "optional " : "");
                off += snprintf(buf + off, len - off, "  %s%s %s = %d;\n",
                                prefix, grpc_proto_type_string(f->type), f->name, f->field_number);
            }
            off += snprintf(buf + off, len - off, "}\n\n");
        }
        off += snprintf(buf + off, len - off, "service %s {\n", svc->name);
        for (int32_t j = 0; j < svc->method_count; j++) {
            grpc_method_def_t* m = &svc->methods[j];
            const char* stream = "";
            if (m->rpc_type == GRPC_SERVER_STREAM) stream = "stream ";
            else if (m->rpc_type == GRPC_CLIENT_STREAM) stream = "stream ";
            else if (m->rpc_type == GRPC_BIDI_STREAM) stream = "stream ";
            off += snprintf(buf + off, len - off, "  rpc %s(%s%s) returns (%s%s);\n",
                            m->name,
                            m->rpc_type == GRPC_CLIENT_STREAM || m->rpc_type == GRPC_BIDI_STREAM ? "stream " : "",
                            m->request_type,
                            m->rpc_type == GRPC_SERVER_STREAM || m->rpc_type == GRPC_BIDI_STREAM ? "stream " : "",
                            m->response_type);
        }
        off += snprintf(buf + off, len - off, "}\n\n");
    }
    return buf;
}

uint32_t grpc_sim_open_stream(grpc_sim_t* g, grpc_rpc_type_t rpc_type) {
    if (!g || g->stream_count >= GRPC_MAX_STREAM_ITEMS) return 0;
    uint32_t id = g->next_stream_id;
    g->next_stream_id += 2;
    grpc_stream_t* s = &g->active_streams[g->stream_count++];
    memset(s, 0, sizeof(*s));
    s->stream_id = id;
    s->rpc_type = rpc_type;
    s->status = GRPC_OK;
    return id;
}

bool grpc_sim_grpc_frame(grpc_stream_t* stream, uint8_t* frame_data, int32_t frame_len) {
    (void)stream;
    (void)frame_data;
    (void)frame_len;
    return true;
}

bool grpc_sim_parse_grpc_frame(grpc_h2_frame_t* frame, const uint8_t* data, int32_t len) {
    if (!frame || !data || len < 9) return false;
    memset(frame, 0, sizeof(*frame));
    frame->payload_len = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | (uint32_t)data[2];
    frame->type = (grpc_h2_frame_type_t)data[3];
    frame->flags = data[4];
    frame->stream_id = ((uint32_t)data[5] << 24) | ((uint32_t)data[6] << 16) |
                       ((uint32_t)data[7] << 8) | (uint32_t)data[8];
    frame->stream_id &= 0x7FFFFFFF;
    int32_t plen = (int32_t)frame->payload_len;
    if (plen > len - 9) plen = len - 9;
    if (plen > (int32_t)sizeof(frame->payload)) plen = (int32_t)sizeof(frame->payload);
    memcpy(frame->payload, data + 9, (size_t)plen);
    return true;
}

bool grpc_sim_build_h2_frame(grpc_h2_frame_t* frame, uint8_t* out, int32_t* out_len, bool is_last) {
    if (!frame || !out || !out_len) return false;
    uint32_t plen = frame->payload_len;
    out[0] = (uint8_t)((plen >> 16) & 0xFF);
    out[1] = (uint8_t)((plen >> 8) & 0xFF);
    out[2] = (uint8_t)(plen & 0xFF);
    out[3] = (uint8_t)frame->type;
    out[4] = (uint8_t)(frame->flags | (is_last ? 0x01 : 0x00));
    out[5] = (uint8_t)((frame->stream_id >> 24) & 0x7F);
    out[6] = (uint8_t)((frame->stream_id >> 16) & 0xFF);
    out[7] = (uint8_t)((frame->stream_id >> 8) & 0xFF);
    out[8] = (uint8_t)(frame->stream_id & 0xFF);
    memcpy(out + 9, frame->payload, plen);
    *out_len = 9 + (int32_t)plen;
    return true;
}

bool grpc_sim_invoke_unary(grpc_sim_t* g, const char* service, const char* method,
                           const uint8_t* req, int32_t req_len,
                           uint8_t* resp, int32_t* resp_len, grpc_status_code_t* status) {
    if (!g || !service || !method) return false;
    (void)req;
    (void)req_len;
    if (resp && resp_len) {
        *resp_len = 0;
    }
    if (status) *status = GRPC_OK;
    return true;
}

bool grpc_sim_open_server_stream(grpc_sim_t* g, const char* service, const char* method,
                                  const uint8_t* req, int32_t req_len, uint32_t* stream_id) {
    if (!g || !service || !method || !stream_id) return false;
    (void)req;
    (void)req_len;
    *stream_id = grpc_sim_open_stream(g, GRPC_SERVER_STREAM);
    return *stream_id != 0;
}

bool grpc_sim_read_stream(grpc_sim_t* g, uint32_t stream_id, uint8_t* data, int32_t* len, bool* done) {
    if (!g) return false;
    for (int32_t i = 0; i < g->stream_count; i++) {
        if (g->active_streams[i].stream_id == stream_id) {
            if (done) *done = false;
            if (data && len && g->active_streams[i].message_len > 0) {
                int32_t clen = *len < g->active_streams[i].message_len ? *len : g->active_streams[i].message_len;
                memcpy(data, g->active_streams[i].message_data, (size_t)clen);
                *len = clen;
            } else {
                if (len) *len = 0;
            }
            return true;
        }
    }
    return false;
}

bool grpc_sim_write_stream(grpc_sim_t* g, uint32_t stream_id, const uint8_t* data, int32_t len) {
    if (!g) return false;
    for (int32_t i = 0; i < g->stream_count; i++) {
        if (g->active_streams[i].stream_id == stream_id) {
            int32_t clen = len > (int32_t)sizeof(g->active_streams[i].message_data)
                               ? (int32_t)sizeof(g->active_streams[i].message_data) : len;
            memcpy(g->active_streams[i].message_data, data, (size_t)clen);
            g->active_streams[i].message_len = clen;
            return true;
        }
    }
    return false;
}

bool grpc_sim_close_stream(grpc_sim_t* g, uint32_t stream_id, grpc_status_code_t status) {
    if (!g) return false;
    for (int32_t i = 0; i < g->stream_count; i++) {
        if (g->active_streams[i].stream_id == stream_id) {
            g->active_streams[i].status = status;
            if ((int)i < g->stream_count - 1) {
                memmove(&g->active_streams[i], &g->active_streams[i + 1],
                        (size_t)(g->stream_count - i - 1) * sizeof(grpc_stream_t));
            }
            g->stream_count--;
            return true;
        }
    }
    return false;
}
