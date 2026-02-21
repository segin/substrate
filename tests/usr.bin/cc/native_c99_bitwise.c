int main(void) {
    int x = 5;
    int y = 3;
    int z = (x & y) + (x | y) + (x ^ y);

    z = z + (1 << 4);
    z = z + (32 >> 2);

    z &= 127;
    z ^= 1;
    z |= 2;

    return z - 39;
}
