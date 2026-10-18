/* every element of a constexpr array's initializer is a constant expression (C23 6.7.1p5) */
double v = 2;
void f(void) { constexpr int x[2] = {1, (int)v}; (void)x; }
