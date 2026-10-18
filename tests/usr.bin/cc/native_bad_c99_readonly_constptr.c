/* a const pointer is not a modifiable lvalue (C11 6.3.2.1p1) */
void f(int *const p, int *q) { p = q; }
