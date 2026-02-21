int main(void) {
    int x = 8;
    int *p = &x;

    *p += 4;
    if (x != 12) {
        return 1;
    }
    *(p + 0) += 1;
    if (x != 13) {
        return 11;
    }
    *p -= 2;
    if (x != 11) {
        return 2;
    }
    *p *= 3;
    if (x != 33) {
        return 3;
    }
    *p /= 5;
    if (x != 6) {
        return 4;
    }
    *p %= 4;
    if (x != 2) {
        return 5;
    }
    *p <<= 3;
    if (x != 16) {
        return 6;
    }
    *p >>= 2;
    if (x != 4) {
        return 7;
    }
    *p |= 1;
    if (x != 5) {
        return 8;
    }
    *p &= 7;
    if (x != 5) {
        return 9;
    }
    *p ^= 6;
    if (x != 3) {
        return 10;
    }

    return 0;
}
