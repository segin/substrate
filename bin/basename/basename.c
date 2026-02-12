#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

int main(int argc, char *argv[]) {
    // Check arguments
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: basename string [suffix]\n");
        return 1;
    }

    char *path = argv[1];

    // POSIX basename() may modify the input string or return a pointer to internal static storage.
    // If it modifies the input string, it's argv[1], which is mutable.
    char *base = basename(path);

    // Handle optional suffix
    if (argc == 3) {
        char *suffix = argv[2];
        size_t base_len = strlen(base);
        size_t suffix_len = strlen(suffix);

        // If suffix is present, is not identical to the characters remaining in string,
        // and is identical to a suffix of the characters remaining in string,
        // the suffix shall be removed.
        if (base_len > suffix_len) {
            if (strcmp(base + base_len - suffix_len, suffix) == 0) {
                base[base_len - suffix_len] = '\0';
            }
        }
    }

    printf("%s\n", base);
    return 0;
}
