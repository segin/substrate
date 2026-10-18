/* a member reached through a pointer to const is not a modifiable lvalue (C11 6.3.2.1p1) */
struct s { int a; };
void f(const struct s *p) { p->a++; }
