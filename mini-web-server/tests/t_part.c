
#include <stdio.h>
#include <string.h>
#include "http_core.h"
extern void test_http_method_str(void);
extern void test_http_method_from_str(void);
extern void test_http_status_text(void);
int main(void) {
    printf("Test start
");
    test_http_method_str();
    test_http_method_from_str();
    test_http_status_text();
    printf("Test end
");
    return 0;
}
