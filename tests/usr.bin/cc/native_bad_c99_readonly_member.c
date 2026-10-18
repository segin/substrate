/* a member of a const structure is not a modifiable lvalue (C11 6.3.2.1p1) */
struct s { int a; };
void f(void) { const struct s v = {1}; v.a = 2; }
