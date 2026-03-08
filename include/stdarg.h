#ifndef _STDARG_H
#define _STDARG_H

typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start((ap), (last))
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, type) __builtin_va_arg((ap), type)
#define va_copy(dst, src) __builtin_va_copy((dst), (src))

#if __STDC_VERSION__ >= 202311L
#define va_start_c23(ap, ...) __builtin_c23_va_start((ap), __VA_ARGS__)
#define va_copy_c23(dst, src) __builtin_c23_va_copy((dst), (src))
#define va_end_c23(ap) __builtin_c23_va_end(ap)
#define va_arg_c23(ap, type) __builtin_c23_va_arg((ap), type)
#endif

#endif
