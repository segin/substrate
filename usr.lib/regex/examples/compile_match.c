#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

int main(void) {
    regex_err_t err;
    const char *pattern = "[a-z]+";
    const char *text = "hello";
    size_t caps[2];

    regex_t *re = regex_compile(pattern, REGEX_FLAG_EXTENDED, &err);
    if (!re) {
        fprintf(stderr, "compile failed: %d\n", err);
        return 1;
    }

    if (regex_match(re, text, strlen(text), caps, 2, &err) >= 0) {
        printf("match: %zu..%zu\n", caps[0], caps[1]);
    } else {
        printf("no match\n");
    }

    regex_free(re);
    return 0;
}
