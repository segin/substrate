int *malloc(unsigned long long n);
void free(int *p);

int main(void) {
    int *base = malloc((unsigned long long)(sizeof(int) * 3));
    int *p;

    if (base == 0) {
        return 1;
    }

    *base = 10;
    *(base + 1) = 20;
    *(base + 2) = 30;

    p = base;
    if (*p++ != 10) {
        free(base);
        return 2;
    }
    if (*p != 20) {
        free(base);
        return 3;
    }
    if (*(++p) != 30) {
        free(base);
        return 4;
    }
    p--;
    if (*p != 20) {
        free(base);
        return 5;
    }

    free(base);
    return 0;
}
