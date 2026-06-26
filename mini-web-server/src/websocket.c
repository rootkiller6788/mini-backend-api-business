#include "websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
 * L5: SHA-1 cryptographic hash (FIPS 180-4).
 * Used for WebSocket handshake: Sec-WebSocket-Accept = base64(sha1(key + GUID)).
 * This is a complete, self-contained SHA-1 implementation.
 *
 * Algorithm:
 *   1. Pad message to 512-bit blocks
 *   2. Process each block through 80 rounds of compression
 *   3. Output 160-bit (20-byte) digest
 */

static uint32_t sha1_rotl(uint32_t val, uint32_t bits) {
    return (val << bits) | (val >> (32 - bits));
}

void ws_sha1(const uint8_t *input, size_t len, uint8_t output[20]) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    uint64_t bit_len = (uint64_t)len * 8;
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = (uint8_t *)calloc(1, padded_len);
    if (!msg) return;

    memcpy(msg, input, len);
    msg[len] = 0x80;

    for (int i = 0; i < 8; i++) {
        msg[padded_len - 1 - i] = (uint8_t)(bit_len >> (i * 8));
    }

    for (size_t off = 0; off < padded_len; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)msg[off + i * 4]     << 24) |
                   ((uint32_t)msg[off + i * 4 + 1] << 16) |
                   ((uint32_t)msg[off + i * 4 + 2] <<  8) |
                   ((uint32_t)msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++) {
            w[i] = sha1_rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = sha1_rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = sha1_rotl(b, 30); b = a; a = temp;
        }

        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    free(msg);

    for (int i = 0; i < 4; i++) output[i]      = (uint8_t)(h0 >> (24 - i * 8));
    for (int i = 0; i < 4; i++) output[i + 4]  = (uint8_t)(h1 >> (24 - i * 8));
    for (int i = 0; i < 4; i++) output[i + 8]  = (uint8_t)(h2 >> (24 - i * 8));
    for (int i = 0; i < 4; i++) output[i + 12] = (uint8_t)(h3 >> (24 - i * 8));
    for (int i = 0; i < 4; i++) output[i + 16] = (uint8_t)(h4 >> (24 - i * 8));
}

/*
 * L5: Base64 encoding (RFC 4648).
 * Used for encoding the SHA-1 digest in WebSocket handshake.
 */
int ws_base64_encode(const uint8_t *input, size_t len,
                      char *output, size_t out_sz) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    size_t out_len = ((len + 2) / 3) * 4;
    if (out_len >= out_sz) return -1;

    for (size_t i = 0, j = 0; i < len; ) {
        uint32_t triple = 0;
        int remaining = (int)(len - i);

        triple |= (uint32_t)input[i++] << 16;
        if (i < len) triple |= (uint32_t)input[i++] << 8;
        if (i < len) triple |= (uint32_t)input[i++];

        output[j++] = tbl[(triple >> 18) & 0x3F];
        output[j++] = tbl[(triple >> 12) & 0x3F];
        output[j++] = (remaining > 1) ? tbl[(triple >> 6) & 0x3F] : '=';
        output[j++] = (remaining > 2) ? tbl[triple & 0x3F]        : '=';
    }

    output[out_len] = '\0';
    return (int)out_len;
}

/*
 * L4: RFC 6455 Sec 4.2.2 - WebSocket handshake validation.
 *
 * The client sends:
 *   GET /chat HTTP/1.1
 *   Host: server.example.com
 *   Upgrade: websocket
 *   Connection: Upgrade
 *   Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
 *   Sec-WebSocket-Version: 13
 *
 * The server responds:
 *   HTTP/1.1 101 Switching Protocols
 *   Upgrade: websocket
 *   Connection: Upgrade
 *   Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
 *
 * Sec-WebSocket-Accept = base64(sha1( Sec-WebSocket-Key | WS_GUID ))
 */
bool ws_validate_handshake(const HttpRequest *req,
                            char *output_accept, size_t accept_sz) {
    const char *upgrade = http_request_get_header(req, "Upgrade");
    if (!upgrade || strcasecmp(upgrade, "websocket") != 0) return 0;

    const char *conn = http_request_get_header(req, "Connection");
    if (!conn || strstr(conn, "Upgrade") == NULL) return 0;

    const char *key = http_request_get_header(req, "Sec-WebSocket-Key");
    if (!key || strlen(key) == 0) return 0;

    const char *ver = http_request_get_header(req, "Sec-WebSocket-Version");
    if (!ver || strcmp(ver, "13") != 0) return 0;

    /* Concatenate key + GUID */
    char combined[256];
    int clen = snprintf(combined, sizeof(combined), "%s%s", key, WS_GUID);
    if (clen < 0 || (size_t)clen >= sizeof(combined)) return 0;

    /* SHA-1 hash */
    uint8_t sha1_digest[20];
    ws_sha1((const uint8_t *)combined, (size_t)clen, sha1_digest);

    /* Base64 encode */
    if (output_accept && accept_sz > 0) {
        ws_base64_encode(sha1_digest, 20, output_accept, accept_sz);
    }
    return 1;
}

void ws_build_handshake_response(const char *accept_key,
                                  const char *protocol,
                                  HttpResponse *res) {
    http_response_set_status(res, 101);
    http_response_add_header(res, "Upgrade", "websocket");
    http_response_add_header(res, "Connection", "Upgrade");
    if (accept_key) {
        http_response_add_header(res, "Sec-WebSocket-Accept", accept_key);
    }
    if (protocol && strlen(protocol) > 0) {
        http_response_add_header(res, "Sec-WebSocket-Protocol", protocol);
    }
}

/*
 * L5: RFC 6455 Sec 5.2 - WebSocket frame encoding.
 *
 * Frame format:
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-------+-+-------------+-------------------------------+
 *  |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 *  |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 *  |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 *  | |1|2|3|       |K|             |                               |
 *  +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 *  |     Extended payload length continued, if payload len == 127  |
 *  + - - - - - - - - - - - - - - - +-------------------------------+
 *  |                               |Masking-key, if MASK set to 1  |
 *  +-------------------------------+-------------------------------+
 *  | Masking-key (continued)       |          Payload Data         |
 *  +-------------------------------- - - - - - - - - - - - - - - - +
 *  :                     Payload Data continued ...                :
 *  + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
 *  |                     Payload Data continued ...                |
 *  +---------------------------------------------------------------+
 */
int ws_encode_frame(const WsFrame *frame, uint8_t *out, size_t out_sz) {
    size_t pos = 0;

    /* Byte 0: FIN + RSV + Opcode */
    out[pos++] = (frame->fin ? 0x80 : 0x00) | (frame->opcode & 0x0F);

    /* Byte 1+: MASK + Payload length */
    size_t plen = (size_t)frame->payload_len;
    if (plen <= WS_MAX_PAYLOAD_126) {
        out[pos++] = (frame->masked ? 0x80 : 0x00) | (uint8_t)plen;
    } else if (plen <= 65535) {
        out[pos++] = (frame->masked ? 0x80 : 0x00) | WS_PAYLOAD_16BIT;
        out[pos++] = (uint8_t)((plen >> 8) & 0xFF);
        out[pos++] = (uint8_t)(plen & 0xFF);
    } else {
        out[pos++] = (frame->masked ? 0x80 : 0x00) | WS_PAYLOAD_64BIT;
        for (int i = 7; i >= 0; i--) {
            out[pos++] = (uint8_t)((plen >> (i * 8)) & 0xFF);
        }
    }

    /* Masking key */
    if (frame->masked) {
        memcpy(out + pos, frame->mask_key, 4);
        pos += 4;
    }

    /* Payload */
    if (pos + plen > out_sz) return -1;
    if (frame->payload_data && plen > 0) {
        memcpy(out + pos, frame->payload_data, plen);
        if (frame->masked) {
            ws_mask_payload(out + pos, plen, frame->mask_key);
        }
    }
    pos += plen;
    return (int)pos;
}

/*
 * L5: WebSocket frame decoding with complete edge case handling.
 * Validates frame structure per RFC 6455 Sec 5.2.
 */
int ws_decode_frame(const uint8_t *data, size_t len, WsFrame *frame) {
    if (!data || !frame || len < 2) return -1;

    memset(frame, 0, sizeof(*frame));

    frame->fin    = (data[0] & 0x80) != 0;
    frame->opcode = data[0] & 0x0F;
    frame->masked = (data[1] & 0x80) != 0;

    size_t pos = 2;
    uint64_t plen = data[1] & 0x7F;

    if (plen == WS_PAYLOAD_16BIT) {
        if (len < 4) return -1;
        plen = ((uint64_t)data[2] << 8) | data[3];
        pos = 4;
    } else if (plen == WS_PAYLOAD_64BIT) {
        if (len < 10) return -1;
        plen = 0;
        for (int i = 0; i < 8; i++) {
            plen = (plen << 8) | data[2 + i];
        }
        pos = 10;
    }

    if (plen > WS_MAX_FRAME_SIZE) return -1;

    /* Read mask */
    if (frame->masked) {
        if (len < pos + 4) return -1;
        memcpy(frame->mask_key, data + pos, 4);
        pos += 4;
    }

    if (len < pos + plen) return -1;

    /* Copy payload */
    if (plen > 0) {
        frame->payload_data = (uint8_t *)malloc((size_t)plen);
        if (!frame->payload_data) return -1;
        memcpy(frame->payload_data, data + pos, (size_t)plen);

        if (frame->masked) {
            ws_mask_payload(frame->payload_data, (size_t)plen, frame->mask_key);
        }
    }
    frame->payload_len = plen;
    return 0;
}

void ws_frame_free(WsFrame *frame) {
    if (frame && frame->payload_data) {
        free(frame->payload_data);
        frame->payload_data = NULL;
        frame->payload_len = 0;
    }
}

/*
 * L5: XOR masking per RFC 6455 Sec 5.3.
 * mask_payload[i] = payload[i] ^ mask_key[i % 4]
 * Applying the mask twice recovers the original data (XOR is its own inverse).
 */
void ws_mask_payload(uint8_t *data, size_t len, const uint8_t mask_key[4]) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= mask_key[i & 3];
    }
}

void ws_generate_mask(uint8_t mask_key[4]) {
    mask_key[0] = (uint8_t)(rand() & 0xFF);
    mask_key[1] = (uint8_t)(rand() & 0xFF);
    mask_key[2] = (uint8_t)(rand() & 0xFF);
    mask_key[3] = (uint8_t)(rand() & 0xFF);
}
