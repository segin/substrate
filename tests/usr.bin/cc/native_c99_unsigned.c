int main(void) {
    unsigned int a = (unsigned int)-1;
    unsigned int b = a / 2u;
    unsigned int c = a >> 1;
    unsigned int d = 3u;

    if (!(a > d)) {
        return 1;
    }
    if (!(a >= d)) {
        return 2;
    }
    if (d >= a) {
        return 3;
    }
    if (b <= 1000u) {
        return 4;
    }
    if (c <= 1000u) {
        return 5;
    }
    if ((a % 2u) != 1u) {
        return 6;
    }

    return 0;
}
