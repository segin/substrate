int main(void) {
    int a = 0b1010;
    long b = 1'000'000;
    double c = 1'2.5'0;
    if (a != 10)
        return 1;
    if (b != 1000000)
        return 2;
    if (c < 12.49 || c > 12.51)
        return 3;
    return 0;
}
