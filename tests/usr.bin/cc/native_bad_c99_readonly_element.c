/* an element of a const array is not a modifiable lvalue (C11 6.3.2.1p1) */
void f(void) { const int a[2] = {1, 2}; a[0] = 3; }
