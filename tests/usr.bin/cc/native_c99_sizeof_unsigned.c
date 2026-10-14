int main(void) {
    int s = 0x2a00;
    int i;
    int n;

    for (i = 0; i < (sizeof(s) - 8); ++i) {
        n = (s >> i) & 0xff;
        if (n == 42) {
            return i == 8 ? 0 : 1;
        }
    }

    return 2;
}
