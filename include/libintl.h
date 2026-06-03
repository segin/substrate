/*
 * libintl.h - message translation (gettext family).
 *
 * Substrate ships the no-NLS ("C" locale) implementation: every lookup
 * returns the msgid unchanged, ngettext picks msgid1/msgid2 by plural rule
 * n==1, and the domain/codeset binders are tracked but load no catalogs.
 * This is the surface GLib, gawk, and other gettext-using software link
 * against; real translation catalogs would require a GNU gettext port.
 */
#ifndef _LIBINTL_H
#define _LIBINTL_H 1

#ifdef __cplusplus
extern "C" {
#endif

char *gettext(const char *__msgid);
char *dgettext(const char *__domainname, const char *__msgid);
char *dcgettext(const char *__domainname, const char *__msgid, int __category);

char *ngettext(const char *__msgid1, const char *__msgid2,
               unsigned long int __n);
char *dngettext(const char *__domainname, const char *__msgid1,
                const char *__msgid2, unsigned long int __n);
char *dcngettext(const char *__domainname, const char *__msgid1,
                 const char *__msgid2, unsigned long int __n, int __category);

char *textdomain(const char *__domainname);
char *bindtextdomain(const char *__domainname, const char *__dirname);
char *bind_textdomain_codeset(const char *__domainname, const char *__codeset);

#ifdef __cplusplus
}
#endif

#endif /* _LIBINTL_H */
