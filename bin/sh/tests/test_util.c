#include "../util.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void test_sh_strndup(void) {
    char *s = "hello world";
    char *d = sh_strndup(s, 5);
    assert(strcmp(d, "hello") == 0);
    free(d);
    
    d = sh_strndup(s, 0);
    assert(strcmp(d, "") == 0);
    free(d);
    
    printf("PASS: test_sh_strndup\n");
}

void test_buffer_append(void) {
    size_t cap = 4, len = 0;
    char *buf = malloc(cap);
    
    buffer_append(&buf, &cap, &len, 'a');
    buffer_append(&buf, &cap, &len, 'b');
    buffer_append(&buf, &cap, &len, 'c');
    buffer_append(&buf, &cap, &len, 'd');
    assert(len == 4);
    assert(cap >= 4);
    
    buffer_append(&buf, &cap, &len, 'e');
    assert(len == 5);
    assert(cap > 4);
    assert(strncmp(buf, "abcde", 5) == 0);
    
    free(buf);
    printf("PASS: test_buffer_append\n");
}

int main(void) {
    test_sh_strndup();
    test_buffer_append();
    return 0;
}
