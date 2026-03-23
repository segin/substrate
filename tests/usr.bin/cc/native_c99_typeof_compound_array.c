#define STATIC_ASSERT(name, expr) typedef char static_assert_##name[(expr) ? 1 : -1]
#define countof(...)                                                                                                     \
    ((unsigned long)(sizeof(__VA_ARGS__) / sizeof((__VA_ARGS__)[0]) +                                                   \
                     0 * sizeof(struct {                                                                                 \
                         unsigned int x                                                                                  \
                             : __builtin_types_compatible_p(typeof(__VA_ARGS__), typeof(&*(__VA_ARGS__))) ? -1 : 1;     \
                     })))

STATIC_ASSERT(compound_literal_is_array, __builtin_types_compatible_p(typeof((int[]){1, 2, 3}), int[3]));
STATIC_ASSERT(compound_literal_not_pointer, !__builtin_types_compatible_p(typeof((int[]){1, 2, 3}), int *));
STATIC_ASSERT(compound_literal_countof, countof((int[]){1, 2, 3}) == 3);

int main(void) {
    return 0;
}
