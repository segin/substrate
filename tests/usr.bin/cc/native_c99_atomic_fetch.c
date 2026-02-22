int main(void) {
    int v = 10;
    int old1;
    int old2;
    old1 = __atomic_fetch_add(&v, 5, __ATOMIC_SEQ_CST);
    old2 = __atomic_fetch_sub(&v, 3, __ATOMIC_SEQ_CST);
    if (old1 != 10) {
        return 1;
    }
    if (old2 != 15) {
        return 2;
    }
    if (v != 12) {
        return 3;
    }
    return 0;
}
