/*
 * nl_types.c — POSIX message catalogs (catopen/catgets/catclose).
 *
 * Substrate ships no message catalogs, so catopen reports "no catalog" and
 * catgets returns the caller's default string.  i18n-aware ported code then
 * runs with its built-in (English) messages.  A real catalog reader can
 * replace this later without changing the interface.
 */

#include <nl_types.h>

nl_catd catopen(const char *name, int flag)
{
    (void)name;
    (void)flag;
    return (nl_catd)-1;   /* no catalog available */
}

char *catgets(nl_catd catd, int set_id, int msg_id, const char *s)
{
    (void)catd;
    (void)set_id;
    (void)msg_id;
    return (char *)s;     /* always the built-in default */
}

int catclose(nl_catd catd)
{
    (void)catd;
    return 0;
}
