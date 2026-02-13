#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <regex.h>

static double now_sec(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

int main(void) {
    regex_err_t err;
    const char *pattern = "[a-z]+";
    const char *text = "abcdefghijklmnopqrstuvwxyz";
    size_t caps[2];
    size_t i;
    double start, end;

    regex_t *re = regex_compile(pattern, REGEX_FLAG_EXTENDED, &err);
    if (!re) {
        fprintf(stderr, "compile failed: %d\n", err);
        return 1;
    }

    start = now_sec();
    for (i = 0; i < 100000; ++i) {
        regex_match(re, text, strlen(text), caps, 2, &err);
    }
    end = now_sec();

    printf("bench: %f sec\n", end - start);
    regex_free(re);
    return 0;
}
