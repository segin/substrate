int *malloc(unsigned long long n);
void free(int *p);

int main(void) {
    int *p = malloc((unsigned long long)(sizeof(int) * 3));
    int *q;
    int *r;

    if (p == 0) {
        return 1;
    }

    q = p + 1;
    r = p + 2;

    if (!(p < q)) {
        free(p);
        return 2;
    }
    if (!(r > q)) {
        free(p);
        return 3;
    }
    if (!(q <= q)) {
        free(p);
        return 4;
    }
    if (!(r >= q)) {
        free(p);
        return 5;
    }

    free(p);
    return 0;
}
