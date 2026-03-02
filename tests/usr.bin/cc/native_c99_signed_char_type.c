int main(void) {
    signed char sc = -1;
    char c = (char)-1;
    unsigned char uc = (unsigned char)-1;

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
    return 0;
}
