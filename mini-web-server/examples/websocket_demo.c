#include "websocket.h"
#include "http_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * L7 Application: WebSocket demo - complete handshake + frame exchange simulation.
 * Demonstrates: handshake validation, frame encoding/decoding, ping/pong.
 *
 * L8 Advanced: Implements the full RFC 6455 opening handshake and demonstrates
 * frame-level protocol operations including control frames (ping/pong/close).
 */

int main(void) {
    printf("=== WebSocket Protocol Demo ===\n\n");

    /* 1. Handshake simulation */
    printf("--- Handshake ---\n");

    HttpRequest req;
    http_request_init(&req);
    http_parse_request_line("GET /chat HTTP/1.1", &req);
    http_parse_header("Host: server.example.com", &req);
    http_parse_header("Upgrade: websocket", &req);
    http_parse_header("Connection: Upgrade", &req);
    http_parse_header("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==", &req);
    http_parse_header("Sec-WebSocket-Version: 13", &req);

    printf("Client request:\n");
    printf("  GET /chat HTTP/1.1\n");
    printf("  Upgrade: websocket\n");
    printf("  Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\n");
    printf("  Sec-WebSocket-Version: 13\n\n");

    char accept_key[64] = {0};
    bool valid = ws_validate_handshake(&req, accept_key, sizeof(accept_key));
    printf("Handshake valid: %s\n", valid ? "YES" : "NO");
    printf("Sec-WebSocket-Accept: %s\n\n", accept_key);

    /* Verify expected accept value */
    printf("Expected:  s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\n");
    printf("Got:       %s\n", accept_key);
    printf("Match:     %s\n\n",
           strcmp(accept_key, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0 ? "YES" : "NO");

    http_request_free(&req);

    /* 2. Frame encoding demo */
    printf("--- Frame Encoding ---\n");

    /* Text frame: "Hello, WebSocket!" */
    WsFrame text_frame;
    memset(&text_frame, 0, sizeof(text_frame));
    text_frame.fin = 1;
    text_frame.opcode = WS_OP_TEXT;
    text_frame.masked = 1;
    text_frame.payload_data = (uint8_t *)"Hello, WebSocket!";
    text_frame.payload_len = strlen((char *)text_frame.payload_data);
    ws_generate_mask(text_frame.mask_key);

    uint8_t wire_buf[256];
    int wire_len = ws_encode_frame(&text_frame, wire_buf, sizeof(wire_buf));

    printf("Text frame: \"Hello, WebSocket!\" (%llu bytes)\n",
           (unsigned long long)text_frame.payload_len);
    printf("Encoded: %d bytes on wire\n", wire_len);
    printf("Hex:    ");
    for (int i = 0; i < wire_len && i < 20; i++) {
        printf("%02x ", wire_buf[i]);
    }
    printf("%s\n\n", wire_len > 20 ? "..." : "");

    /* 3. Frame decoding demo */
    printf("--- Frame Decoding ---\n");

    WsFrame decoded;
    int rc = ws_decode_frame(wire_buf, (size_t)wire_len, &decoded);
    if (rc == 0) {
        printf("Decoded: FIN=%d, Opcode=%d, Len=%llu\n",
               decoded.fin, decoded.opcode, (unsigned long long)decoded.payload_len);
        if (decoded.payload_data) {
            printf("Payload: \"%.*s\"\n\n",
                   (int)decoded.payload_len, decoded.payload_data);
        }
    }
    ws_frame_free(&decoded);

    /* 4. Ping/Pong control frames */
    printf("--- Control Frames (Ping/Pong) ---\n");

    WsFrame ping;
    memset(&ping, 0, sizeof(ping));
    ping.fin = 1;
    ping.opcode = WS_OP_PING;
    ping.masked = 1;
    ping.payload_data = (uint8_t *)"keepalive";
    ping.payload_len = 9;
    ws_generate_mask(ping.mask_key);

    uint8_t ping_wire[64];
    int ping_len = ws_encode_frame(&ping, ping_wire, sizeof(ping_wire));
    printf("Ping encoded: %d bytes\n", ping_len);

    WsFrame ping_decoded;
    rc = ws_decode_frame(ping_wire, (size_t)ping_len, &ping_decoded);
    if (rc == 0) {
        printf("Ping decoded: opcode=%d, payload=\"%.*s\"\n",
               ping_decoded.opcode,
               (int)ping_decoded.payload_len,
               ping_decoded.payload_data);
    }
    ws_frame_free(&ping_decoded);

    /* 5. Close frame */
    printf("\n--- Close Frame ---\n");

    WsFrame close_frame;
    memset(&close_frame, 0, sizeof(close_frame));
    close_frame.fin = 1;
    close_frame.opcode = WS_OP_CLOSE;
    close_frame.masked = 1;
    /* Close payload: 2-byte status code + optional reason */
    uint8_t close_payload[] = { 0x03, 0xe8, 'b', 'y', 'e' }; /* 1000 + "bye" */
    close_frame.payload_data = close_payload;
    close_frame.payload_len = 5;
    ws_generate_mask(close_frame.mask_key);

    uint8_t close_wire[64];
    int close_len = ws_encode_frame(&close_frame, close_wire, sizeof(close_wire));
    printf("Close frame encoded: %d bytes\n", close_len);

    WsFrame close_decoded;
    rc = ws_decode_frame(close_wire, (size_t)close_len, &close_decoded);
    if (rc == 0) {
        printf("Close decoded: opcode=%d, payload_len=%llu\n",
               close_decoded.opcode,
               (unsigned long long)close_decoded.payload_len);
        if (close_decoded.payload_len >= 2) {
            uint16_t code = ((uint16_t)close_decoded.payload_data[0] << 8) |
                             close_decoded.payload_data[1];
            printf("Close status code: %u\n", code);
        }
    }
    ws_frame_free(&close_decoded);

    /* 6. SHA-1 and Base64 demo */
    printf("\n--- SHA-1 Demo ---\n");
    const char *test_inputs[] = {"abc", "Hello", "mini-web-server"};
    for (int i = 0; i < 3; i++) {
        uint8_t hash[20];
        ws_sha1((const uint8_t *)test_inputs[i], strlen(test_inputs[i]), hash);
        char b64[32];
        ws_base64_encode(hash, 20, b64, sizeof(b64));
        printf("SHA-1(\"%s\") = ... -> base64: %s\n", test_inputs[i], b64);
    }

    /* 7. Invalid handshake test */
    printf("\n--- Invalid Handshake Test ---\n");
    HttpRequest bad_req;
    http_request_init(&bad_req);
    http_parse_request_line("GET / HTTP/1.1", &bad_req);

    char bad_accept[64];
    bool bad_valid = ws_validate_handshake(&bad_req, bad_accept, sizeof(bad_accept));
    printf("Non-websocket request: %s\n", bad_valid ? "valid?!" : "correctly rejected");

    http_request_free(&bad_req);

    printf("\n=== Done ===\n");
    return 0;
}
