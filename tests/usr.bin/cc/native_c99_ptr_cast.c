int *malloc(unsigned long long n);
void free(int *p);

int main(void) {
    int *p = malloc((unsigned long long)(sizeof(int) * 2));
    void *vp;
    unsigned long long addr;
    int *q;

    if (p == 0) {
        return 1;
    }

    *p = 11;
    vp = (void *)p;
    addr = (unsigned long long)vp;
    q = (int *)addr;
    if (q != p) {
        free(p);
        return 2;
    }
    if (*q != 11) {
        free(p);
        return 3;
    }
    if ((unsigned long long)sizeof(int *) != (unsigned long long)sizeof(void *)) {
        free(p);
        return 4;
    }

    free(p);
    return 0;
}
