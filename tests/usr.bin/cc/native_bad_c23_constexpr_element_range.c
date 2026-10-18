/* each element of a constexpr array keeps its initializer's value (C23 6.7.1p6): -129 is no signed char */
constexpr signed char x[] = {-128, 127, -129};
