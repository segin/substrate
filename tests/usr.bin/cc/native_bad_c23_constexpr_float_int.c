/* a constexpr float's integer initializer keeps its value in float (C23 6.7.1p6): 2^24 + 1 does not */
void f(void) { constexpr float x = 16777217; (void)x; }
