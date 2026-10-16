/*
 * _BitInt sizes as the psABI lays them out: up to 64 bits, the smallest
 * of 1, 2, 4 and 8 bytes that holds the width.  A signed _BitInt needs at
 * least 2 bits (one for the sign); unsigned _BitInt(1) is valid.
 */
int main(void) {
    if (sizeof(unsigned _BitInt(1)) != 1)
        return 1;
    if (sizeof(_BitInt(2)) != 1)
        return 2;
    if (sizeof(_BitInt(9)) != 2)
        return 3;
    if (sizeof(unsigned _BitInt(33)) != 8)
        return 4;
    if (_Alignof(unsigned _BitInt(33)) != sizeof(void *))
        return 5;
    return 0;
}
