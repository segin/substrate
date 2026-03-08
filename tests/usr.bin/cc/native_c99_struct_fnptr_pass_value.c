struct pair {
    int a;
    int b;
};

static int call_pair(int (*fn)(struct pair), struct pair p) {
    return fn(p);
}

static int sum_pair(struct pair p) {
    return p.a + p.b;
}

int main(void) {
    struct pair p = {3, 4};
    if (call_pair(sum_pair, p) != 7) {
        return 1;
    }
    return 0;
}
