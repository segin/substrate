int main(void) {
    int x = 5;
    int *p = &x;

    if (!p) {
        return 1;
    }
    if (*p != 5) {
        return 2;
    }

    x = 7;
    if (*p != 7) {
        return 3;
    }

    if ((p == 0) || (0 == p)) {
        return 4;
    }

    return 0;
}
