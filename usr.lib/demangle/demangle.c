#include <demangle.h>

#include <stdlib.h>
#include <string.h>

#include "demangle_internal.h"

#define DEMANGLE_SCHEME_MASK (DEMANGLE_AUTO | DEMANGLE_ITANIUM | DEMANGLE_RUST | DEMANGLE_DLANG)
#define DEMANGLE_SUPPORTED_MASK (DEMANGLE_NO_PARAMS | DEMANGLE_NO_VERBOSE | DEMANGLE_TYPES | DEMANGLE_SCHEME_MASK)

static int
starts_with(const char *s, const char *prefix)
{
    size_t n;

    if (s == NULL || prefix == NULL) {
        return 0;
    }

    n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

static int
normalize_options(int options, unsigned *out)
{
    unsigned u;
    unsigned schemes;

    if (out == NULL) {
        return -1;
    }

    u = (unsigned)options;
    if ((u & ~DEMANGLE_SUPPORTED_MASK) != 0u) {
        return -1;
    }

    schemes = u & DEMANGLE_SCHEME_MASK;
    if (schemes == 0u) {
        u |= DEMANGLE_AUTO;
        schemes = DEMANGLE_AUTO;
    }

    if ((schemes & (schemes - 1u)) != 0u) {
        return -1;
    }

    *out = u;
    return 0;
}

static char *
demangle_dispatch(const char *mangled, unsigned options)
{
    char *out;

    if (mangled == NULL || mangled[0] == '\0') {
        return NULL;
    }

    if ((options & DEMANGLE_ITANIUM) != 0u) {
        return demangle_itanium(mangled, (int)options);
    }

    if ((options & DEMANGLE_RUST) != 0u) {
        return demangle_rust(mangled, (int)options);
    }

    if ((options & DEMANGLE_DLANG) != 0u) {
        return demangle_dlang(mangled, (int)options);
    }

    if ((options & DEMANGLE_AUTO) == 0u) {
        return NULL;
    }

    if (starts_with(mangled, "_R")) {
        out = demangle_rust(mangled, (int)options);
        if (out != NULL) {
            return out;
        }
        return demangle_itanium(mangled, (int)options);
    }

    if (starts_with(mangled, "_ZN")) {
        out = demangle_rust(mangled, (int)options);
        if (out != NULL) {
            return out;
        }
    }

    if (starts_with(mangled, "_D")) {
        out = demangle_dlang(mangled, (int)options);
        if (out != NULL) {
            return out;
        }
        return demangle_itanium(mangled, (int)options);
    }

    if (starts_with(mangled, "_d_")) {
        return demangle_dlang(mangled, (int)options);
    }

    if (starts_with(mangled, "_Z") || (options & DEMANGLE_TYPES) != 0u) {
        return demangle_itanium(mangled, (int)options);
    }

    return NULL;
}

char *
demangle(const char *mangled, int options)
{
    unsigned normalized;

    if (mangled == NULL || mangled[0] == '\0') {
        return NULL;
    }

    if (normalize_options(options, &normalized) != 0) {
        return NULL;
    }

    return demangle_dispatch(mangled, normalized);
}

int
demangle_buf(const char *mangled, char *buf, size_t bufsz, int options)
{
    char *tmp;
    size_t need;

    if (buf == NULL) {
        return -1;
    }

    if (bufsz > 0u) {
        buf[0] = '\0';
    }

    tmp = demangle(mangled, options);
    if (tmp == NULL) {
        return -1;
    }

    need = strlen(tmp) + 1u;

    if (bufsz == 0u) {
        demangle_free(tmp);
        return -2;
    }

    if (need > bufsz) {
        memcpy(buf, tmp, bufsz - 1u);
        buf[bufsz - 1u] = '\0';
        demangle_free(tmp);
        return -2;
    }

    memcpy(buf, tmp, need);
    demangle_free(tmp);
    return 0;
}

void
demangle_free(char *str)
{
    free(str);
}

const char *
demangle_version(void)
{
    return "libdemangle 0.1.0";
}
