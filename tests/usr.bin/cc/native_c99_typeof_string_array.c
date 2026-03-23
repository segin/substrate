#define STATIC_ASSERT(name, expr) typedef char static_assert_##name[(expr) ? 1 : -1]
#define countof(...)                                                                                                     \
    ((unsigned long)(sizeof(__VA_ARGS__) / sizeof((__VA_ARGS__)[0]) +                                                   \
                     0 * sizeof(struct {                                                                                 \
                         unsigned int x                                                                                  \
                             : __builtin_types_compatible_p(typeof(__VA_ARGS__), typeof(&*(__VA_ARGS__))) ? -1 : 1;     \
                     })))

STATIC_ASSERT(string_literal_is_array, __builtin_types_compatible_p(typeof("x"), char[2]));
STATIC_ASSERT(string_literal_not_pointer, !__builtin_types_compatible_p(typeof("x"), char *));
STATIC_ASSERT(string_literal_countof, countof("string") == 7);
STATIC_ASSERT(wide_string_literal_countof, countof(L"wide string") == 12);

int main(void) {
    return 0;
}
