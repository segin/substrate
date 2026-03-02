int main(void) {
    _Decimal32 d32 = 1.0;
    _Decimal64 d64 = 2.0;
    _Decimal128 d128 = 3.0;

    if (sizeof(d32) != 4)
        return 1;
    if (sizeof(d64) != 8)
        return 2;
    if (sizeof(d128) != 16)
        return 3;
    if ((int)d32 != 1 || (int)d64 != 2 || (int)d128 != 3)
        return 4;
    return 0;
}
