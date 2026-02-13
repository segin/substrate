#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

int main(void) {
    regex_err_t err;
    const char *pattern = "[0-9]+";
    const char *text = "id=42";
    char *out = NULL;
    size_t out_len = 0;

    regex_t *re = regex_compile(pattern, REGEX_FLAG_EXTENDED, &err);
    if (!re) {
        fprintf(stderr, "compile failed: %d\n", err);
        return 1;
    }

    if (regex_replace(re, text, strlen(text), "<num>$0</num>", 0, &out, &out_len) == REGEX_OK) {
        printf("%s\n", out);
        free(out);
    }

    regex_free(re);
    return 0;
}
