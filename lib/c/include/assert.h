#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef NDEBUG
#define assert(condition) ((void)0)
#else
void __assert_fail(const char *expr, const char *file, int line, const char *func);
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

#define static_assert _Static_assert

#endif
