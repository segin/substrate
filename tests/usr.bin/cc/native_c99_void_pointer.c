int main(void) {
    int x = 42;
    int *ip = &x;
    void *vp = ip;
    int *ip2 = vp;

    if (vp == 0) {
        return 1;
    }
    if (ip2 == 0) {
        return 2;
    }
    if (*ip2 != 42) {
        return 3;
    }

    return 0;
}
