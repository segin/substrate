/* a constexpr floating object's initializer is a constant expression (C23 6.7.1p5) */
void f(void) { double y = 1; constexpr double x = y; (void)x; }
