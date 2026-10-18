/* a constexpr object's initializer is a constant expression (C23 6.7.10p6) */
void f(int n) { constexpr int a = n; (void)a; }
