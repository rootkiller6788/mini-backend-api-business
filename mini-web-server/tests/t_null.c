#include <stdio.h>
#include <stdlib.h>
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("hello from null test\n");
    return 0;
}
