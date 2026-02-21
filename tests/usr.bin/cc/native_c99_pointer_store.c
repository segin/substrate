int main(void) {
    int x = 1;
    int *p = &x;

    *p = 9;
    if (x != 9) {
        return 1;
    }

    *p = *p + 3;
    if (x != 12) {
        return 2;
    }

    return 0;
}
