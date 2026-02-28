#include "demangle_internal.h"

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
dlang_is_mangled(const char *mangled)
{
    return starts_with(mangled, "_D");
}

static char *
dlang_demangle_symbol(const char *mangled, int options)
{
    (void)mangled;
    (void)options;
    return (char *)0;
}

char *
demangle_dlang(const char *mangled, int options)
{
    if (mangled == NULL || mangled[0] == '\0') {
        return NULL;
    }

    if ((options & DEMANGLE_DLANG) != 0) {
        if (!dlang_is_mangled(mangled)) {
            return NULL;
        }
        return dlang_demangle_symbol(mangled, options);
    }

    if (dlang_is_mangled(mangled)) {
        return dlang_demangle_symbol(mangled, options);
    }

    return NULL;
}
