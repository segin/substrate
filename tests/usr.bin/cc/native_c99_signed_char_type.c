int main(void) {
    signed char sc = -1;
    char c = (char)-1;
    unsigned char uc = (unsigned char)-1;
    int widened = (signed char)128;
    int wait_style = (((signed char)(((0x137f & 0x7f) + 1))) >> 1) > 0;

    if (sizeof(sc) != 1)
        return 1;
    if (sc != -1)
        return 2;
    if (uc != 255)
        return 3;

    if (_Generic(sc, signed char : 1, default : 0) != 1)
        return 4;
    if (_Generic(c, char : 1, default : 0) != 1)
        return 5;
    if (_Generic(uc, unsigned char : 1, default : 0) != 1)
        return 6;
    if (widened != -128)
        return 7;
    if (wait_style != 0)
        return 8;
    return 0;
}
