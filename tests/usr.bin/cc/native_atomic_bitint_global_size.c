/*
 * An _Atomic or _BitInt object at file scope is as big as its type:
 * `_Atomic short` is 2 bytes and `_BitInt(17)` 4, so .data is 8 bytes
 * on both targets, as gcc lays it out.  cc gave each the pointer size,
 * 16 bytes of .data at -m64.  The suite checks the .data section's
 * size; the program checks the values.
 */
_Atomic short as = 1;
_BitInt(17) b17 = 5;

int main(void) {
    return as == 1 && b17 == 5 ? 0 : 1;
}
