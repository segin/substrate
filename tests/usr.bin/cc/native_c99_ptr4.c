static int read_pppp(int ****pppp) {
    return ****pppp;
}

static void write_pppp(int ****pppp, int v) {
    ****pppp = v;
}

int main(void) {
    int x = 13;
    int *p = &x;
    int **pp = &p;
    int ***ppp = &pp;
    int ****pppp = &ppp;

    if (read_pppp(pppp) != 13) {
        return 1;
    }
    write_pppp(pppp, 19);
    if (x != 19) {
        return 2;
    }
    if (****pppp != 19) {
        return 3;
    }
    return 0;
}
