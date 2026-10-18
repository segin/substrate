/*
 * `int b : 24` between `char a` and `char c` takes bytes 1..3, and no 1-,
 * 2-, 4- or 8-byte unit holds them without a or c, each a memory location
 * of its own (C11 3.14) another thread may be writing.  So the store to b
 * reads the 4-byte unit at a but writes back only bytes 1, 2 and 3, one
 * byte store each, as gcc does; it never stores 4 bytes over a.
 */
struct s {
    char a;
    int b : 24;
    char c;
};

void put(struct s *p, int v) {
    p->b = v;
}
