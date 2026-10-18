/* every part of a constexpr aggregate's initializer is a constant expression (C23 6.7.1p5) */
struct s { int a; double b; };
void f(void) { double y = 1; constexpr struct s x = {1, y}; (void)x; }
