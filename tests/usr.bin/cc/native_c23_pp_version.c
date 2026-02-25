#if __STDC_VERSION__ != 202311L
#error unexpected __STDC_VERSION__
#endif

#if __has_c_attribute(deprecated)
int has_deprecated = 1;
#else
int has_deprecated = 0;
#endif

#if __has_c_attribute(nodiscard)
int has_nodiscard = 1;
#else
int has_nodiscard = 0;
#endif

#if __has_c_attribute(this_is_not_real)
int has_unknown = 1;
#else
int has_unknown = 0;
#endif

int stdc_version = __STDC_VERSION__;
