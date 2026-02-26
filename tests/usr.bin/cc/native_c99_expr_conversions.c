int main(void) {
    unsigned short us = 65535;
    short ss = -1;
    int x = us + ss;
    int y = (int)3.75;
    int z = (1, 2, 3);
    int local = 42;
    int *p = &local;
    int *q = (1 ? p : 0);

    if ((int)sizeof(us + ss) != (int)sizeof(int)) {
        return 1;
    }
    if (x != 65534) {
        return 2;
    }
    if (((int)-1 < (unsigned int)1) != 0) {
        return 3;
    }
    if ((int)sizeof(((short)1) << (long long)1) != (int)sizeof(int)) {
        return 4;
    }
    if (y != 3) {
        return 5;
    }
    if (z != 3) {
        return 6;
    }
    if (q != p) {
        return 7;
    }
    return 0;
}
