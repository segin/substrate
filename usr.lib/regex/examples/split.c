#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

int main(void) {
    regex_err_t err;
    const char *pattern = ",";
    const char *text = "a,b,c";
    regex_split_result_t result;
    size_t i;

    regex_t *re = regex_compile(pattern, REGEX_FLAG_LITERAL, &err);
    if (!re) {
        fprintf(stderr, "compile failed: %d\n", err);
        return 1;
    }

    if (regex_split(re, text, strlen(text), &result, 0) == REGEX_OK) {
        for (i = 0; i < result.count; ++i) {
            printf("[%zu] %s\n", i, result.items[i]);
        }
        regex_split_free(&result);
    }

    regex_free(re);
    return 0;
}
