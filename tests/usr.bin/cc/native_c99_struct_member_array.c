struct holder {
    int tag;
    int *vals[4];
    int done;
};

int main(void) {
    int a = 7;
    int b = 9;
    struct holder h;

    h.tag = 1;
    h.vals[0] = &a;
    h.vals[1] = &b;
    h.vals[2] = 0;
    h.vals[3] = 0;
    h.done = 1;

    if (h.tag != 1 || h.done != 1) {
        return 1;
    }
    if (*h.vals[0] + *h.vals[1] != 16) {
        return 2;
    }
    if (h.vals[2] != 0 || h.vals[3] != 0) {
        return 3;
    }
    return 0;
}
