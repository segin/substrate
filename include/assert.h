/*
 * <assert.h> — C standard §7.2.
 *
 * This header is intentionally re-includable: the `assert` macro is
 * (re)defined according to the CURRENT state of NDEBUG on EVERY inclusion.
 * Only the one-time declarations (the __assert_fail prototype and the
 * `static_assert` alias) sit behind an include-once guard; the `assert`
 * macro itself must live OUTSIDE that guard.  Code such as gnulib does
 *
 *     #include <assert.h>     // assert active
 *     #undef assert
 *     #include <assert.h>     // must restore assert
 *
 * and the classic
 *
 *     #define NDEBUG
 *     #include <assert.h>     // assert now a no-op
 *
 * both depend on this.  A single-include guard around the macro (the old
 * behaviour) left `assert` undefined on the second include, so any TU that
 * relied on it hit "implicit declaration of function 'assert'".
 */

#ifndef _ASSERT_H_DECLS
#define _ASSERT_H_DECLS

#ifdef __cplusplus
extern "C" {
#endif

void __assert_fail(const char *expr, const char *file, unsigned int line,
                   const char *func);

#ifdef __cplusplus
}
#endif

/*
 * C11 spells the compile-time assertion _Static_assert; expose the C-style
 * `static_assert` alias.  C++ already reserves `static_assert` as a keyword,
 * so we MUST NOT define the macro under C++ — it would rewrite every C++
 * static_assert into the C keyword and break the parse.
 */
#ifndef __cplusplus
#ifndef static_assert
#define static_assert _Static_assert
#endif
#endif

#endif /* _ASSERT_H_DECLS — one-time declarations only */

/*
 * The assert macro is (re)defined on every inclusion, per §7.2.  Undefine any
 * prior definition first so a re-include restores it even after a caller did
 * `#undef assert`.
 */
#undef assert

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif
