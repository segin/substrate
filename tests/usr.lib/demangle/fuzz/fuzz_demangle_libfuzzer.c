#include <demangle.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *sym;
    char *out;

    if (data == NULL || size == 0u) {
        return 0;
    }

    sym = (char *)malloc(size + 1u);
    if (sym == NULL) {
        return 0;
    }

    memcpy(sym, data, size);
    sym[size] = '\0';

    out = demangle(sym, DEMANGLE_AUTO);
    free(out);
    out = demangle(sym, DEMANGLE_ITANIUM);
    free(out);
    out = demangle(sym, DEMANGLE_RUST);
    free(out);
    out = demangle(sym, DEMANGLE_DLANG);
    free(out);

    free(sym);
    return 0;
}
