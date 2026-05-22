#include "http_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("=== HTTP Parse Demo ===\n\n");

    /* 1. Parse a request line */
    HttpRequest req;
    http_request_init(&req);

    const char *rl = "GET /api/users?id=42&name=alice HTTP/1.1";
    if (http_parse_request_line(rl, &req)) {
        printf("[REQUEST LINE]\n");
        printf("  Method:       %s\n", http_method_str(req.method));
        printf("  Path:         %s\n", req.path);
        printf("  Query String: %s\n\n", req.query_string);
    } else {
        printf("FAILED to parse request line\n");
    }

    /* 2. Parse headers */
    const char *h1 = "Host: localhost:8080";
    const char *h2 = "Content-Type: application/json";
    const char *h3 = "Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.xxx";
    const char *h4 = "Accept-Encoding: gzip, deflate";
    http_parse_header(h1, &req);
    http_parse_header(h2, &req);
    http_parse_header(h3, &req);
    http_parse_header(h4, &req);

    printf("[HEADERS] %d parsed\n", req.header_count);
    for (int i = 0; i < req.header_count; i++) {
        printf("  %s: %s\n", req.headers[i].name, req.headers[i].value);
    }

    /* 3. Get specific headers */
    const char *host = http_request_get_header(&req, "Host");
    const char *auth = http_request_get_header(&req, "Authorization");
    printf("\n[LOOKUP]\n");
    printf("  Host:          %s\n", host ? host : "(null)");
    printf("  Authorization: %s\n", auth ? auth : "(null)");

    /* 4. Parse query string */
    char key_buf[256], val_buf[256];
    if (http_parse_query_string(req.query_string, key_buf, sizeof(key_buf),
                                  val_buf, sizeof(val_buf), "id")) {
        printf("\n[QUERY] id = %s\n", val_buf);
    }
    if (http_parse_query_string(req.query_string, key_buf, sizeof(key_buf),
                                  val_buf, sizeof(val_buf), "name")) {
        printf("[QUERY] name = %s\n", val_buf);
    }

    /* 5. Build response */
    HttpResponse res;
    http_response_init(&res);
    http_response_set_status(&res, HTTP_STATUS_OK);
    http_response_add_header(&res, "Content-Type", "application/json");
    http_response_add_header(&res, "X-Request-Id", "abc-123");
    http_response_set_body_str(&res, "{\"status\":\"ok\"}");

    printf("\n[RESPONSE SERIALIZED]\n");
    char serialized[4096];
    int len = http_serialize_response(&res, serialized, sizeof(serialized));
    if (len > 0) {
        serialized[len] = '\0';
        printf("%s\n", serialized);
    }

    /* 6. Status code table */
    printf("[STATUS CODES]\n");
    int codes[] = {200, 201, 301, 304, 400, 401, 403, 404, 500, 503};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        printf("  %d -> %s\n", codes[i], http_status_text(codes[i]));
    }

    /* 7. URL decode */
    char decoded[256];
    http_url_decode("hello%20world%21%20%2F%20%3F%20%26%20%3D",
                     decoded, sizeof(decoded));
    printf("\n[URL DECODE]\n  'hello%%20world%%21%%20%%2F%%20%%3F%%20%%26%%20%%3D'\n");
    printf("  -> '%s'\n", decoded);

    http_request_free(&req);
    http_response_free(&res);
    printf("\n=== Done ===\n");
    return 0;
}
