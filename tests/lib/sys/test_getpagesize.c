#include <stdio.h>
#include <assert.h>

int getpagesize(void);

void test_getpagesize(void) {
    printf("Testing getpagesize()...\n");
    int pagesize = getpagesize();
    assert(pagesize == 4096);
    printf("PASS: getpagesize() returned %d\n", pagesize);
}

int main(void) {
    test_getpagesize();
    return 0;
}
