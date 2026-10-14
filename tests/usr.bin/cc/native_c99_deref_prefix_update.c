int main(void) {
    int x = 4;
    int *p = &x;

    if (++*p != 5) {
        return 1;
    }
    if (x != 5) {
        return 2;
    }
    if (++*(p + 0) != 6) {
        return 3;
    }
    if (x != 6) {
        return 4;
    }
    if (--*p != 5) {
        return 5;
    }
    if (x != 5) {
        return 6;
    }

    return 0;
}
