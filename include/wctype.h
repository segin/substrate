#ifndef _WCTYPE_H
#define _WCTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>

typedef unsigned int wctype_t;

int iswalnum(wint_t wc);
int iswalpha(wint_t wc);
int iswblank(wint_t wc);
int iswcntrl(wint_t wc);
int iswdigit(wint_t wc);
int iswgraph(wint_t wc);
int iswlower(wint_t wc);
int iswprint(wint_t wc);
int iswpunct(wint_t wc);
int iswspace(wint_t wc);
int iswupper(wint_t wc);
int iswxdigit(wint_t wc);

wint_t towlower(wint_t wc);
wint_t towupper(wint_t wc);

wctype_t wctype(const char *property);
int iswctype(wint_t wc, wctype_t desc);

/* wctrans — wide-character mapping (locale-specific transliteration).
 * Substrate doesn't have full locale support; wctrans only handles
 * the standard "tolower" / "toupper" properties. */
typedef int wctrans_t;
wctrans_t wctrans(const char *property);
wint_t    towctrans(wint_t wc, wctrans_t desc);

#ifdef __cplusplus
}
#endif

#endif /* _WCTYPE_H */
