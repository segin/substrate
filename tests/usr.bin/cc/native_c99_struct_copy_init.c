struct pair {
    int a;
    int b;
};

int main(void) {
    struct pair p = {7, 9};
    struct pair *pp = &p;
    struct pair q = *pp;

    if (q.a != 7 || q.b != 9) {
        return 1;
    }
    return 0;
}
