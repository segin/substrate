/* a floating member of a constexpr structure keeps its initializer's value (C23 6.7.1p6): 0.1 is no float */
struct in {
    float f;
};
struct s {
    int a;
    struct in in[2];
};
void f(void) { constexpr struct s x = {1, {{0.5}, {0.1}}}; (void)x; }
