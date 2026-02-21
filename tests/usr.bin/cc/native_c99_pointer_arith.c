int *malloc(unsigned long long n);
void free(int *p);

static int sum_tail(int *p) {
    int *q = p + 1;
    int *r = 1 + p;
    int *s = r + 1;
    return *q + *s;
}

int main(void) {
    int *p = malloc((unsigned long long)(sizeof(int) * 3));
    if (p == 0) {
        return 1;
    }

    *p = 4;
    *(p + 1) = 7;
    *(p + 2) = 9;

    if ((1 + p) != (p + 1)) {
        free(p);
        return 2;
    }
    if (*((p + 2) - 1) != 7) {
        free(p);
        return 3;
    }
    if (sum_tail(p) != 16) {
        free(p);
        return 4;
    }

    free(p);
    return 0;
}
