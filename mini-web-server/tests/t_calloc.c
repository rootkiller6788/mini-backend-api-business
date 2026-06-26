#include <stdio.h>
#include <stdlib.h>
#include "http_core.h"
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("About to calloc HttpRequest (%zu bytes)...\n", sizeof(HttpRequest));
    HttpRequest *req = calloc(1, sizeof(HttpRequest));
    printf("calloc returned: %p\n", (void*)req);
    if (req) {
        http_request_init(req);
        printf("init done, size check: %d\n", (int)sizeof(req->headers[0]));
        http_parse_request_line("GET / HTTP/1.1", req);
        printf("parse done\n");
        http_request_free(req); free(req);
    }
    printf("Done\n");
    return 0;
}
