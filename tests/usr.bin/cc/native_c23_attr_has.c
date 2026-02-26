#if __has_c_attribute(deprecated)
int has_deprecated_attr = 1;
#else
int has_deprecated_attr = 0;
#endif

#if __has_c_attribute(nodiscard)
int has_nodiscard_attr = 1;
#else
int has_nodiscard_attr = 0;
#endif

#if __has_c_attribute(reproducible)
int has_reproducible_attr = 1;
#else
int has_reproducible_attr = 0;
#endif

#if __has_c_attribute(unsequenced)
int has_unsequenced_attr = 1;
#else
int has_unsequenced_attr = 0;
#endif
