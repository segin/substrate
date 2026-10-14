static int read_ppp(int ***ppp) {
    return ***ppp;
}

static void write_ppp(int ***ppp, int v) {
    ***ppp = v;
}

int main(void) {
    int x = 11;
    int *p = &x;
    int **pp = &p;
    int ***ppp = &pp;

    if (read_ppp(ppp) != 11) {
        return 1;
    }
    write_ppp(ppp, 17);
    if (x != 17) {
        return 2;
    }
    if (***ppp != 17) {
        return 3;
    }
    return 0;
}
