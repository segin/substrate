#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

int main(void) {
    regex_err_t err;
    const char *pattern = "[a-z]+";
    const char *chunk1 = "hello ";
    const char *chunk2 = "world";
    size_t start, end;
    size_t caps[2];
    size_t cap_count;

    regex_t *re = regex_compile(pattern, REGEX_FLAG_EXTENDED, &err);
    if (!re) {
        fprintf(stderr, "compile failed: %d\n", err);
        return 1;
    }

    regex_iter_t *it = regex_iter_create(re, REGEX_ITER_DEFAULT, &err);
    if (!it) {
        fprintf(stderr, "iter create failed: %d\n", err);
        regex_free(re);
        return 1;
    }

    regex_iter_feed(it, chunk1, strlen(chunk1));
    regex_iter_feed(it, chunk2, strlen(chunk2));
    regex_iter_finish(it);

    while (regex_iter_next(it, &start, &end, caps, 2, &cap_count) > 0) {
        printf("match %zu..%zu\n", start, end);
    }

    regex_iter_destroy(it);
    regex_free(re);
    return 0;
}
