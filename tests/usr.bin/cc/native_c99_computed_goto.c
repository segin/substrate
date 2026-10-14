static int dispatch(int idx) {
    static void *jt[] = {&&L0, &&L1, &&L2};
    void *p;
    if (idx < 0 || idx > 2) {
        idx = 2;
    }
    p = jt[idx];
    goto *p;
L0:
    return 10;
L1:
    return 20;
L2:
    return 30;
}

int main(void) {
    if (dispatch(0) != 10) {
        return 1;
    }
    if (dispatch(1) != 20) {
        return 2;
    }
    if (dispatch(2) != 30) {
        return 3;
    }
    if (dispatch(99) != 30) {
        return 4;
    }
    return 0;
}
