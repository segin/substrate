int main(void) {
    int x = 3;
    int *p = &x;

    if ((*p)++ != 3) {
        return 1;
    }
    if (x != 4) {
        return 2;
    }
    if ((*(p + 0))++ != 4) {
        return 3;
    }
    if (x != 5) {
        return 4;
    }
    if ((*p)-- != 5) {
        return 5;
    }
    if (x != 4) {
        return 6;
    }
    if ((*(p + 0))-- != 4) {
        return 7;
    }
    if (x != 3) {
        return 8;
    }

    return 0;
}
