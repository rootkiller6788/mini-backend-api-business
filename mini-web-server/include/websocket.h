#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "http_core.h"

/*
 * L1 - Core Definitions: WebSocket frame format (RFC 6455), opcodes, handshake
 * L2 - Core Concepts: Full-duplex persistent connection, protocol upgrade from HTTP
 * L3 - Engineering Structures: Frame encode/decode pipeline with masking
 * L4 - Standards/Theorems: RFC 6455 WebSocket Protocol specification
 * L5 - Algorithms: SHA-1 hash for Sec-WebSocket-Accept, XOR masking/unmasking
 * L6 - Canonical Problem: Real-time bidirectional communication over TCP
 * L7 - Application: Chat server backend, live data feeds
 * L8 - Advanced: WebSocket extensions (permessage-deflate), subprotocols
 * L9 - Industry: WebSocket vs HTTP/2 Server-Sent Events trade-offs
 */

#define WS_MAX_FRAME_SIZE        (64 * 1024)
#define WS_MAX_MESSAGE_SIZE      (256 * 1024)
#define WS_GUID                  "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_MAX_PAYLOAD_126       125
#define WS_PAYLOAD_16BIT         126
#define WS_PAYLOAD_64BIT         127

/* ── WebSocket Opcodes (RFC 6455 Sec 5.2) ─────────────────────────────────── */
typedef enum {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT         = 0x1,
    WS_OP_BINARY       = 0x2,
    WS_OP_CLOSE        = 0x8,
    WS_OP_PING         = 0x9,
    WS_OP_PONG         = 0xA
} WsOpcode;

/* ── WebSocket Frame (RFC 6455 Sec 5.2) ───────────────────────────────────── */
typedef struct {
    bool     fin;
    bool     masked;
    uint8_t  opcode;
    uint64_t payload_len;
    uint8_t  mask_key[4];
    uint8_t *payload_data;
} WsFrame;

/* ── WebSocket Connection State ──────────────────────────────────────────── */
typedef enum {
    WS_STATE_CONNECTING,
    WS_STATE_OPEN,
    WS_STATE_CLOSING,
    WS_STATE_CLOSED
} WsState;

typedef struct {
    WsState  state;
    char     url[2048];
    char     protocol[64];
    uint8_t  recv_buf[WS_MAX_FRAME_SIZE];
    size_t   recv_len;
} WsConnection;

/* ── Handshake ───────────────────────────────────────────────────────────── */

/*
 * Given an HTTP Upgrade request, validate the WebSocket handshake
 * and generate the required Sec-WebSocket-Accept response header value.
 * Returns true if the request is a valid WebSocket upgrade.
 * output_accept must be at least 29 bytes.
 */
bool ws_validate_handshake(const HttpRequest *req,
                            char *output_accept, size_t accept_sz);

/*
 * Build a complete HTTP 101 Switching Protocols response for WebSocket upgrade.
 */
void ws_build_handshake_response(const char *accept_key,
                                  const char *protocol,
                                  HttpResponse *res);

/* ── Frame Encode/Decode ─────────────────────────────────────────────────── */

/* Encode a WebSocket frame into wire format. Returns bytes written. */
int  ws_encode_frame(const WsFrame *frame, uint8_t *out, size_t out_sz);

/* Decode a raw buffer into a WsFrame. Returns 0 on success, -1 on error. */
int  ws_decode_frame(const uint8_t *data, size_t len, WsFrame *frame);

/* Free any internally allocated memory in a WsFrame. */
void ws_frame_free(WsFrame *frame);

/* ── Masking ────────────────────────────────────────────────────────────── */

/* Apply XOR masking to payload data in-place (RFC 6455 Sec 5.3). */
void ws_mask_payload(uint8_t *data, size_t len, const uint8_t mask_key[4]);

/* Generate random 4-byte mask key. */
void ws_generate_mask(uint8_t mask_key[4]);

/* ── SHA-1 Helper ────────────────────────────────────────────────────────── */

/* Compute SHA-1 hash of input. output must be at least 20 bytes. */
void ws_sha1(const uint8_t *input, size_t len, uint8_t output[20]);

/* ── Base64 Encode ───────────────────────────────────────────────────────── */

/* Base64-encode raw bytes. Returns output length. */
int  ws_base64_encode(const uint8_t *input, size_t len,
                       char *output, size_t out_sz);

#endif /* WEBSOCKET_H */
