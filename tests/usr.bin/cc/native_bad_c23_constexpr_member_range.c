/* each member of a constexpr structure keeps its initializer's value (C23 6.7.1p6): 256 is no unsigned char */
struct s {
    int a;
    unsigned char c;
};
void f(void) { constexpr struct s x = {1, 256}; (void)x; }
