struct pair {
    long long a;
    int b;
    char c;
};

int main(void) {
    struct pair p;
    struct pair q;

    p.a = 0x1122334455667788LL;
    p.b = 12345;
    p.c = 'Z';

    q.a = 0;
    q.b = 0;
    q.c = 0;

    q = p;

    if (q.a != p.a || q.b != p.b || q.c != p.c) {
        return 1;
    }
    p.b = 7;
    if (q.b == p.b) {
        return 2;
    }
    return 0;
}
