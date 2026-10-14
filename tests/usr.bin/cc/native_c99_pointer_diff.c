int *malloc(unsigned long long n);
void free(int *p);

int main(void) {
    int *p = malloc((unsigned long long)(sizeof(int) * 8));
    int *q;
    int d1;
    int d2;

    if (p == 0) {
        return 1;
    }

    q = p + 5;
    d1 = q - p;
    d2 = p - q;
    if (d1 != 5) {
        free(p);
        return 2;
    }
    if (d2 != -5) {
        free(p);
        return 3;
    }

    free(p);
    return 0;
}
