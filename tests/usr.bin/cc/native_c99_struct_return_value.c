struct pair {
    int a;
    int b;
};

static struct pair mk_pair(int a, int b) {
    struct pair p;
    p.a = a;
    p.b = b;
    return p;
}

static struct pair add_pair(struct pair x, struct pair y) {
    return mk_pair(x.a + y.a, x.b + y.b);
}

int main(void) {
    struct pair a = mk_pair(5, 8);
    struct pair b = mk_pair(7, 9);
    struct pair c = add_pair(a, b);
    if (c.a != 12) {
        return 1;
    }
    if (c.b != 17) {
        return 2;
    }
    return 0;
}
