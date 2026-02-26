int main(void) {
    double x = 0x1.8p1;
    float y = 0x1.0p2f;

    if (x < 2.9 || x > 3.1) {
        return 1;
    }
    if (y < 3.9f || y > 4.1f) {
        return 2;
    }
    return 0;
}
