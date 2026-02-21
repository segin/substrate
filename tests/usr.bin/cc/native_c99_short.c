int main(void) {
    short a = 9;
    unsigned short b = 7;

    if (sizeof(short) != 2) {
        return 1;
    }
    if (sizeof(unsigned short) != 2) {
        return 2;
    }
    if ((a - 4) != 5) {
        return 3;
    }
    if ((b + 5u) != 12u) {
        return 4;
    }
    return 0;
}
