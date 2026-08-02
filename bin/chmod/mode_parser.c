#include "mode_parser.h"
#include "modeparse.h"

struct chmod_mode *
chmod_setmode(const char *mode_string, char *errbuf, size_t errbuf_len)
{
    return (struct chmod_mode *)modeparse_compile(mode_string, errbuf,
        errbuf_len);
}

mode_t
chmod_getmode(const struct chmod_mode *mode, mode_t old_mode)
{
    return modeparse_apply((const struct mode_change *)mode, old_mode);
}

void
chmod_freemode(struct chmod_mode *mode)
{
    modeparse_free((struct mode_change *)mode);
}
