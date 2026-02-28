#include <demangle.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
    char line[8192];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *out;
        size_t n = strlen(line);
        while (n > 0u && (line[n - 1u] == '\n' || line[n - 1u] == '\r')) {
            line[--n] = '\0';
        }

        out = demangle(line, DEMANGLE_AUTO);
        if (out != NULL) {
            puts(out);
            free(out);
        } else {
            puts(line);
        }
    }

    return 0;
}
