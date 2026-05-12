#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
#define assert(condition) ((void)0)
#else
void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func);
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

/*
 * C11 spells it _Static_assert; C99 and earlier had no spelling.
 * <assert.h> is required to expose the C-style alias `static_assert`
 * as a macro.  C++ already reserves `static_assert` as a keyword, so
 * we MUST NOT define the macro under C++ — it would rewrite every
 * C++ static_assert into the C keyword and break parse.
 */
#ifndef __cplusplus
#define static_assert _Static_assert
#endif

#endif
