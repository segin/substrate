struct pair {
    int a;
    int b;
};

static int sum_pair(struct pair p) {
    return p.a + p.b;
}

static int mix_pair(int x, struct pair p, int y) {
    return x + (p.a * 10) + p.b + y;
}

int main(void) {
    struct pair p = {3, 4};
    if (sum_pair(p) != 7) {
        return 1;
    }
    if (mix_pair(1, p, 2) != 37) {
        return 2;
    }
    return 0;
}
