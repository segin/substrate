int main(void) {
    char a = (char)0xf7;
    unsigned char b = (unsigned char)0xf7;

    if (a != (char)0xf7)
        return 1;
    if ((long)a != -9L)
        return 2;
    if (b != 247U)
        return 3;
    if ((long)(char)b != -9L)
        return 4;
    return 0;
}
