/* what a pointer to const points at is not a modifiable lvalue (C11 6.3.2.1p1) */
void f(const int *p) { *p = 1; }
