/* a constexpr float's initializer keeps its value in float (C23 6.7.1p6): 0.1 does not */
void f(void) { constexpr float x = 0.1; (void)x; }
