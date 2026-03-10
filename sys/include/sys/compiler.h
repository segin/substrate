#ifndef _SYS_COMPILER_H
#define _SYS_COMPILER_H

#if defined(__GNUC__)
#define SUB_NODISCARD __attribute__((warn_unused_result))
#define SUB_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#define SUB_PURE __attribute__((pure))
#else
#define SUB_NODISCARD
#define SUB_NONNULL(...)
#define SUB_PURE
#endif

#endif /* _SYS_COMPILER_H */
