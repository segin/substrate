#include <demangle.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    char line[8192];
    int options;

    options = DEMANGLE_AUTO;
    if (argc > 1 && strcmp(argv[1], "--types") == 0) {
        options |= DEMANGLE_TYPES;
    }

    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t n;
        char *out;

        n = strlen(line);
        while (n > 0u && (line[n - 1u] == '\n' || line[n - 1u] == '\r')) {
            line[--n] = '\0';
        }

        out = demangle(line, options);
        if (out != NULL) {
            puts(out);
            free(out);
        } else {
            puts("");
        }
    }

    return 0;
}
