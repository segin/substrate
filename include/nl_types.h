#ifndef _NL_TYPES_H
#define _NL_TYPES_H

/*
 * <nl_types.h> — POSIX message-catalog API (catopen/catgets/catclose).
 *
 * Substrate has no installed message catalogs, so catgets always returns the
 * caller-supplied default string (the built-in, English text).  Ported i18n
 * code (CDE/ToolTalk) compiles and runs untranslated.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef void *nl_catd;
typedef int   nl_item;

#define NL_SETD        1
#define NL_CAT_LOCALE  1

nl_catd catopen(const char *name, int flag);
char   *catgets(nl_catd catd, int set_id, int msg_id, const char *s);
int     catclose(nl_catd catd);

#ifdef __cplusplus
}
#endif
#endif /* _NL_TYPES_H */
