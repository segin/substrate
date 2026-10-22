int main(void) {
    int x = 5;
    int y = 3;
    int one = 1; /* a variable: a shift of constants is folded at compile time */
    int z = (x & y) + (x | y) + (x ^ y);

    z = z + (one << 4);
    z = z + (32 >> 2);

    z &= 127;
    z ^= 1;
    z |= 2;

    return z - 39;
}
