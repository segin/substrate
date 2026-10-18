/* a constexpr pointer is null (C23 6.7.1p5) */
int y;
constexpr int *q = &y;
