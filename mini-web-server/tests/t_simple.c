#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_core.h"

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stderr, "Starting test...\n");
    printf("Hello stdout\n");
    fprintf(stderr, "After printf\n");

    HttpRequest *req = calloc(1, sizeof(HttpRequest));
    if (!req) { fprintf(stderr, "calloc failed\n"); return 1; }
    fprintf(stderr, "Allocated req at %p\n", (void*)req);

    http_request_init(req);
    fprintf(stderr, "Initialized req\n");

    http_parse_request_line("GET / HTTP/1.1", req);
    fprintf(stderr, "Parsed request line\n");

    printf("Method: %s\n", http_method_str(req->method));
    fprintf(stderr, "Done\n");

    http_request_free(req);
    free(req);
    return 0;
}
