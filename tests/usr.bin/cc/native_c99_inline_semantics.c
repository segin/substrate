/*
 * C99 inline (C11 6.7.4p7).  A file-scope `inline` definition with no
 * `extern` declaration is an inline definition only: no external symbol
 * (add_local, which is not called, since a call to it may use an
 * external definition that does not exist).  `extern inline` (add_ext),
 * or an `extern` declaration beside an inline definition (add_both),
 * makes it an external definition.
 */
inline int add_local(int a, int b) {
    return a + b;
}

extern inline int add_ext(int a, int b) {
    return a + b + 1;
}

inline int add_both(int a, int b) {
    return a + b + 2;
}
extern int add_both(int a, int b);

int main(void) {
    return add_ext(2, 3) + add_both(2, 3) - 13;
}
