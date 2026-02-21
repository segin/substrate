static int read_pp(int **pp) {
    return **pp;
}

static void set_pp(int **dst, int *src) {
    *dst = src;
}

int main(void) {
    int x = 5;
    int y = 8;
    int *p = &x;
    int **pp = &p;

    if (read_pp(pp) != 5) {
        return 1;
    }
    set_pp(pp, &y);
    if (read_pp(pp) != 8) {
        return 2;
    }
    if (*pp != &y) {
        return 3;
    }
    if (**pp != 8) {
        return 4;
    }

    return 0;
}
