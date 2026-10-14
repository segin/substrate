struct bitpack {
    unsigned a:3;
    signed int b:5;
    unsigned c:6;
};

int main(void) {
    struct bitpack v;
    v.a = 7;
    v.b = -3;
    v.c = 35;
    if (v.a != 7) {
        return 1;
    }
    if (v.b != -3) {
        return 2;
    }
    if (v.c != 35) {
        return 3;
    }
    v.b = v.b + 1;
    if (v.b != -2) {
        return 4;
    }
    v.a = 0;
    if (v.b != -2 || v.c != 35) {
        return 5;
    }
    return 0;
}
