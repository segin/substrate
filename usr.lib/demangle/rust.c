#include "demangle_internal.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include <demangle.h>

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
is_hex_char(char ch)
{
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

static int
rust_is_v0(const char *mangled)
{
    return starts_with(mangled, "_R");
}

static int
rust_is_legacy(const char *mangled)
{
    size_t len;
    size_t i;

    if (!starts_with(mangled, "_ZN")) {
        return 0;
    }

    len = strlen(mangled);
    if (len < 24u || mangled[len - 1u] != 'E') {
        return 0;
    }

    if (mangled[len - 20u] != '1' || mangled[len - 19u] != '7' || mangled[len - 18u] != 'h') {
        return 0;
    }

    for (i = len - 17u; i < len - 1u; i++) {
        if (!is_hex_char(mangled[i])) {
            return 0;
        }
    }

    return 1;
}

static char *
rust_demangle_v0(const char *mangled, int options)
{
    (void)mangled;
    (void)options;
    return (char *)0;
}

static char *
rust_demangle_legacy(const char *mangled, int options)
{
    (void)mangled;
    (void)options;
    return (char *)0;
}

char *
demangle_rust(const char *mangled, int options)
{
    if (mangled == NULL || mangled[0] == '\0') {
        return NULL;
    }

    if ((options & DEMANGLE_RUST) != 0) {
        if (!rust_is_v0(mangled)) {
            return NULL;
        }
        return rust_demangle_v0(mangled, options);
    }

    if (rust_is_v0(mangled)) {
        return rust_demangle_v0(mangled, options);
    }

    if (rust_is_legacy(mangled)) {
        return rust_demangle_legacy(mangled, options);
    }

    return NULL;
}
