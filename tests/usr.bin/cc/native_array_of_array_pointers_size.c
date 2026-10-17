/*
 * An array of pointers to arrays holds pointers.  cc took `rows` below for
 * the 2x4 array of int it points into: it laid it out at that array's
 * size (64 bytes at -m64, not 16) and read `rows[i]` as the address of
 * the element instead of loading the pointer in it, so `*rows[1]` was
 * not g[1].  The suite checks the .data section's size (`rows` is all of
 * it; `g` has no initializer, so it is in .bss); the program checks the
 * values.
 */
int g[2][4];
int (*rows[2])[4] = {&g[0], &g[1]};

int main(void) {
    (*rows[1])[3] = 7;
    return g[1][3] == 7 && rows[1] - rows[0] == 1 && rows[0] == &g[0] ? 0 : 1;
}
