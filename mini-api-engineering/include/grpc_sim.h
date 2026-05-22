#ifndef GRPC_SIM_H
#define GRPC_SIM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define GRPC_MAX_SERVICE_NAME    128
#define GRPC_MAX_METHOD_NAME     128
#define GRPC_MAX_MESSAGE_NAME    128
#define GRPC_MAX_FIELDS          64
#define GRPC_MAX_METHODS         32
#define GRPC_MAX_SERVICES        16
#define GRPC_MAX_FRAME_SIZE      65536
#define GRPC_MAX_STREAM_ITEMS    256
#define GRPC_MAX_METADATA_ITEMS  32
#define GRPC_MAX_PROTO_LEN       16384

typedef enum {
    GRPC_UNARY         = 0,
    GRPC_SERVER_STREAM = 1,
    GRPC_CLIENT_STREAM = 2,
    GRPC_BIDI_STREAM   = 3
} grpc_rpc_type_t;

typedef enum {
    GRPC_OK               = 0,
    GRPC_CANCELLED        = 1,
    GRPC_UNKNOWN          = 2,
    GRPC_INVALID_ARGUMENT = 3,
    GRPC_DEADLINE_EXCEEDED= 4,
    GRPC_NOT_FOUND        = 5,
    GRPC_ALREADY_EXISTS   = 6,
    GRPC_PERMISSION_DENIED= 7,
    GRPC_RESOURCE_EXHAUSTED=8,
    GRPC_FAILED_PRECONDITION=9,
    GRPC_ABORTED          = 10,
    GRPC_OUT_OF_RANGE     = 11,
    GRPC_UNIMPLEMENTED    = 12,
    GRPC_INTERNAL         = 13,
    GRPC_UNAVAILABLE      = 14,
    GRPC_DATA_LOSS        = 15,
    GRPC_UNAUTHENTICATED  = 16
} grpc_status_code_t;

typedef enum {
    GRPC_PROTO_DOUBLE   = 1,
    GRPC_PROTO_FLOAT    = 2,
    GRPC_PROTO_INT64    = 3,
    GRPC_PROTO_UINT64   = 4,
    GRPC_PROTO_INT32    = 5,
    GRPC_PROTO_FIXED64  = 6,
    GRPC_PROTO_FIXED32  = 7,
    GRPC_PROTO_BOOL     = 8,
    GRPC_PROTO_STRING   = 9,
    GRPC_PROTO_BYTES    = 12,
    GRPC_PROTO_UINT32   = 13,
    GRPC_PROTO_ENUM     = 14,
    GRPC_PROTO_SFIXED32 = 15,
    GRPC_PROTO_SFIXED64 = 16,
    GRPC_PROTO_SINT32   = 17,
    GRPC_PROTO_SINT64   = 18,
    GRPC_PROTO_MESSAGE  = 99
} grpc_proto_type_t;

typedef struct {
    char             name[GRPC_MAX_MESSAGE_NAME];
    grpc_proto_type_t type;
    int32_t          field_number;
    bool             is_repeated;
    bool             is_optional;
    char             message_type[GRPC_MAX_MESSAGE_NAME];
} grpc_field_def_t;

typedef struct {
    char            name[GRPC_MAX_MESSAGE_NAME];
    grpc_field_def_t fields[GRPC_MAX_FIELDS];
    int32_t         field_count;
} grpc_message_def_t;

typedef struct {
    char            name[GRPC_MAX_METHOD_NAME];
    grpc_rpc_type_t rpc_type;
    char            request_type[GRPC_MAX_MESSAGE_NAME];
    char            response_type[GRPC_MAX_MESSAGE_NAME];
} grpc_method_def_t;

typedef struct {
    char             name[GRPC_MAX_SERVICE_NAME];
    char             package[256];
    grpc_method_def_t methods[GRPC_MAX_METHODS];
    int32_t          method_count;
} grpc_service_def_t;

typedef struct {
    char key[128];
    char value[512];
} grpc_metadata_t;

typedef enum {
    GRPC_FRAME_DATA          = 0x00,
    GRPC_FRAME_HEADERS       = 0x01,
    GRPC_FRAME_PRIORITY      = 0x02,
    GRPC_FRAME_RST_STREAM    = 0x03,
    GRPC_FRAME_SETTINGS      = 0x04,
    GRPC_FRAME_PUSH_PROMISE  = 0x05,
    GRPC_FRAME_PING          = 0x06,
    GRPC_FRAME_GOAWAY        = 0x07,
    GRPC_FRAME_WINDOW_UPDATE = 0x08,
    GRPC_FRAME_CONTINUATION  = 0x09
} grpc_h2_frame_type_t;

typedef struct {
    grpc_h2_frame_type_t type;
    uint8_t              flags;
    uint32_t             stream_id;
    uint32_t             payload_len;
    uint8_t              payload[GRPC_MAX_FRAME_SIZE];
} grpc_h2_frame_t;

typedef struct {
    uint32_t         stream_id;
    grpc_rpc_type_t  rpc_type;
    grpc_status_code_t status;
    char             status_message[256];
    grpc_metadata_t  metadata[GRPC_MAX_METADATA_ITEMS];
    int32_t          metadata_count;
    uint8_t          message_data[GRPC_MAX_FRAME_SIZE];
    int32_t          message_len;
    bool             is_compressed;
} grpc_stream_t;

typedef struct {
    grpc_service_def_t  services[GRPC_MAX_SERVICES];
    int32_t             service_count;
    grpc_message_def_t  messages[GRPC_MAX_SERVICES * GRPC_MAX_METHODS * 2];
    int32_t             message_count;
} grpc_proto_file_t;

typedef struct {
    grpc_proto_file_t proto;
    grpc_stream_t     active_streams[GRPC_MAX_STREAM_ITEMS];
    int32_t           stream_count;
    uint32_t          next_stream_id;
} grpc_sim_t;

const char* grpc_rpc_type_string(grpc_rpc_type_t t);
const char* grpc_status_string(grpc_status_code_t c);
const char* grpc_proto_type_string(grpc_proto_type_t t);
const char* grpc_h2_frame_type_string(grpc_h2_frame_type_t t);

void grpc_sim_init(grpc_sim_t* g);
grpc_service_def_t* grpc_sim_add_service(grpc_sim_t* g, const char* name, const char* package);
grpc_method_def_t* grpc_sim_add_method(grpc_service_def_t* svc, const char* name, grpc_rpc_type_t rpc_type,
                                       const char* req_type, const char* resp_type);
grpc_message_def_t* grpc_sim_add_message(grpc_sim_t* g, const char* name);
void grpc_message_add_field(grpc_message_def_t* msg, const char* name, grpc_proto_type_t type,
                            int32_t field_number, bool repeated, bool optional);

char* grpc_sim_export_proto(grpc_sim_t* g, char* buf, size_t len);

uint32_t grpc_sim_open_stream(grpc_sim_t* g, grpc_rpc_type_t rpc_type);
bool grpc_sim_grpc_frame(grpc_stream_t* stream, uint8_t* frame_data, int32_t frame_len);
bool grpc_sim_parse_grpc_frame(grpc_h2_frame_t* frame, const uint8_t* data, int32_t len);
bool grpc_sim_build_h2_frame(grpc_h2_frame_t* frame, uint8_t* out, int32_t* out_len, bool is_last);

bool grpc_sim_invoke_unary(grpc_sim_t* g, const char* service, const char* method,
                           const uint8_t* req, int32_t req_len,
                           uint8_t* resp, int32_t* resp_len, grpc_status_code_t* status);

bool grpc_sim_open_server_stream(grpc_sim_t* g, const char* service, const char* method,
                                  const uint8_t* req, int32_t req_len, uint32_t* stream_id);
bool grpc_sim_read_stream(grpc_sim_t* g, uint32_t stream_id, uint8_t* data, int32_t* len, bool* done);
bool grpc_sim_write_stream(grpc_sim_t* g, uint32_t stream_id, const uint8_t* data, int32_t len);
bool grpc_sim_close_stream(grpc_sim_t* g, uint32_t stream_id, grpc_status_code_t status);

#endif
