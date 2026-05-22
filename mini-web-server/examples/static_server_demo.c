#include "static_serve.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    const char *root = (argc > 1) ? argv[1] : "./public";
    printf("=== Static File Server Demo ===\n");
    printf("Root directory: %s\n\n", root);

    /* 1. Initialize static config */
    StaticConfig cfg;
    static_config_init(&cfg, root);
    static_config_load_default_mimes(&cfg);
    printf("[CONFIG] %d MIME types loaded\n", cfg.mime_count);
    printf("  Caching: %s\n", cfg.enable_caching ? "on" : "off");
    printf("  Range:   %s\n", cfg.enable_range ? "on" : "off");
    printf("  Index:   %s\n\n", cfg.index_file);

    /* 2. MIME type lookup */
    const char *test_paths[] = {
        "/index.html", "/style.css", "/app.js", "/logo.png",
        "/data.json", "/video.mp4", "/archive.zip", "/readme.txt",
        "/unknown.xyz", "/noext"
    };
    for (size_t i = 0; i < sizeof(test_paths) / sizeof(test_paths[0]); i++) {
        printf("[MIME] %-20s -> %s\n",
               test_paths[i], static_get_mime_type(&cfg, test_paths[i]));
    }

    /* 3. Simulate serving a known file */
    printf("\n[SIMULATED REQUEST] /index.html\n");
    HttpRequest req;
    http_request_init(&req);
    http_parse_request_line("GET /index.html HTTP/1.1", &req);
    http_parse_header("Host: localhost", &req);

    HttpResponse res;
    http_response_init(&res);

    if (static_serve_file(&cfg, "/index.html", &req, &res)) {
        printf("  Status:  %d %s\n", res.status_code,
               http_status_text(res.status_code));
        for (int i = 0; i < res.header_count; i++) {
            printf("  %s: %s\n", res.headers[i].name, res.headers[i].value);
        }
        if (res.body && res.body_len > 0) {
            printf("  Body:   %zu bytes\n", res.body_len);
            /* print first 128 chars */
            size_t preview = res.body_len > 128 ? 128 : res.body_len;
            printf("  Preview: %.*s...\n", (int)preview, res.body);
        }
    } else {
        printf("  Serve failed (file may not exist)\n");
    }

    http_request_free(&req);
    http_response_free(&res);

    /* 4. Simulate 404 */
    printf("\n[SIMULATED 404] /missing.html\n");
    HttpRequest req2;
    http_request_init(&req2);
    http_parse_request_line("GET /missing.html HTTP/1.1", &req2);

    HttpResponse res2;
    http_response_init(&res2);

    static_serve_file(&cfg, "/missing.html", &req2, &res2);
    printf("  Status: %d %s\n", res2.status_code,
           http_status_text(res2.status_code));

    http_request_free(&req2);
    http_response_free(&res2);

    /* 5. Simulate range request */
    printf("\n[SIMULATED RANGE] bytes=0-1023 for /video.mp4\n");
    HttpRequest req3;
    http_request_init(&req3);
    http_parse_request_line("GET /video.mp4 HTTP/1.1", &req3);
    http_parse_header("Range: bytes=0-1023", &req3);

    HttpResponse res3;
    http_response_init(&res3);

    if (static_serve_file(&cfg, "/video.mp4", &req3, &res3)) {
        printf("  Status: %d %s\n", res3.status_code,
               http_status_text(res3.status_code));
        const char *cr = NULL;
        for (int i = 0; i < res3.header_count; i++) {
            printf("  %s: %s\n", res3.headers[i].name, res3.headers[i].value);
            if (strcmp(res3.headers[i].name, "Content-Range") == 0)
                cr = res3.headers[i].value;
        }
        printf("  Content-Range: %s\n", cr ? cr : "(none)");
    }

    http_request_free(&req3);
    http_response_free(&res3);

    /* 6. ETag and cache demo */
    printf("\n[ETAG / CACHE]\n");
    FileCacheInfo info;
    info.file_size = 102400;
    info.mtime = 1715000000;
    static_build_etag("/style.css", info.mtime, info.file_size,
                       info.etag, sizeof(info.etag));
    strftime(info.last_modified, sizeof(info.last_modified),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&info.mtime));

    printf("  File:     /style.css\n");
    printf("  Size:     %llu bytes\n", (unsigned long long)info.file_size);
    printf("  ETag:     %s\n", info.etag);
    printf("  Modified: %s\n", info.last_modified);

    /* Check If-None-Match */
    HttpRequest req4;
    http_request_init(&req4);
    http_parse_header("If-None-Match: \"1234-abcd\"", &req4);
    bool not_mod = static_check_not_modified(&req4, &info);
    printf("  If-None-Match \"1234-abcd\" match ETag \"%s\": %s\n",
           info.etag, not_mod ? "304" : "200");

    http_request_free(&req4);

    printf("\n=== Done ===\n");
    return 0;
}
