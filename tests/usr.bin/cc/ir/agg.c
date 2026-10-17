/* aggregates: a structure passed, returned and copied */
struct pt { int x, y, z; };
struct pt mk(int v) { struct pt p = { v, v + 1, v + 2 }; return p; }
int sumpt(struct pt p) { return p.x + p.y + p.z; }
int use(void) { struct pt q = mk(3); return sumpt(q); }
