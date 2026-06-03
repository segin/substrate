/*
 * libintl.c - no-NLS message translation for substrate libc.
 *
 * The "C" locale implementation of the gettext(3) family: lookups return the
 * msgid unchanged and no message catalogs are consulted.  The domain and
 * codeset binders record their arguments (so callers that read them back get
 * a sensible answer) but do not load anything.  This is enough to build and
 * run gettext-using software (GLib, etc.) without translations.
 */
#include <libintl.h>
#include <string.h>
#include <stdlib.h>

/* Single tracked text domain (gettext's default is "messages"). */
static char *cur_domain;
/* One remembered binding: domain -> directory and -> codeset.  A single
 * slot is plenty for the no-NLS case; bindtextdomain returns the directory
 * and bind_textdomain_codeset returns the codeset, as the API requires. */
static char *bound_dir;
static char *bound_codeset;

static char *dup_or_null(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *gettext(const char *msgid)
{
    return (char *)msgid;
}

char *dgettext(const char *domainname, const char *msgid)
{
    (void)domainname;
    return (char *)msgid;
}

char *dcgettext(const char *domainname, const char *msgid, int category)
{
    (void)domainname; (void)category;
    return (char *)msgid;
}

char *ngettext(const char *msgid1, const char *msgid2, unsigned long int n)
{
    /* Germanic plural rule (n == 1 is singular) — the gettext default. */
    return (char *)(n == 1 ? msgid1 : msgid2);
}

char *dngettext(const char *domainname, const char *msgid1,
                const char *msgid2, unsigned long int n)
{
    (void)domainname;
    return (char *)(n == 1 ? msgid1 : msgid2);
}

char *dcngettext(const char *domainname, const char *msgid1,
                 const char *msgid2, unsigned long int n, int category)
{
    (void)domainname; (void)category;
    return (char *)(n == 1 ? msgid1 : msgid2);
}

char *textdomain(const char *domainname)
{
    if (domainname) {
        char *d = dup_or_null(domainname);
        if (d) { free(cur_domain); cur_domain = d; }
    }
    return cur_domain ? cur_domain : (char *)"messages";
}

char *bindtextdomain(const char *domainname, const char *dirname)
{
    (void)domainname;
    if (dirname) {
        char *d = dup_or_null(dirname);
        if (d) { free(bound_dir); bound_dir = d; }
    }
    return bound_dir;          /* NULL until first bound, per the API */
}

char *bind_textdomain_codeset(const char *domainname, const char *codeset)
{
    (void)domainname;
    if (codeset) {
        char *c = dup_or_null(codeset);
        if (c) { free(bound_codeset); bound_codeset = c; }
    }
    return bound_codeset;
}
