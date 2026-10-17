/*
 * A bit-field is accessed through the smallest unit that holds it and no
 * byte of an ordinary member beside it, which is a memory location of its
 * own (C11 3.14) another thread may be writing: storing `b` must not
 * rewrite `a` or `e`.  The stores below are one byte (b, f) and two bytes
 * (g), never the four of the field's type.
 */
struct s {
    char a;
    int b : 3;
    short e;
};

struct t {
    char c;
    unsigned f : 8;
    unsigned g : 12;
    char d;
};

void set_b(struct s *p, int v) { p->b = v; }

void set_f(struct t *p, unsigned v) { p->f = v; }

void set_g(struct t *p, unsigned v) { p->g = v; }

int get_b(const struct s *p) { return p->b; }
