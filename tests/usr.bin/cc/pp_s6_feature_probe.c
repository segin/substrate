#if __has_feature(c99)
int hf = 1;
#else
int hf = 0;
#endif

#if __has_extension(gnu_statement_expression)
int he = 1;
#else
int he = 0;
#endif

#if __has_builtin(__builtin_expect)
int hb = 1;
#else
int hb = 0;
#endif

#if __has_include(<stddef.h>)
int hi = 1;
#else
int hi = 0;
#endif

#if __has_attribute(noreturn)
int ha = 1;
#else
int ha = 0;
#endif

#if __has_c_attribute(deprecated)
int hca = 1;
#else
int hca = 0;
#endif

#if __has_declspec_attribute(noinline)
int hda = 1;
#else
int hda = 0;
#endif

#if __has_warning("-Wall")
int hw = 1;
#else
int hw = 0;
#endif

#if __is_identifier(restrict)
int iid_kw = 1;
#else
int iid_kw = 0;
#endif

#if __is_identifier(my_custom_ident)
int iid_user = 1;
#else
int iid_user = 0;
#endif
