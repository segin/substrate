void *malloc(unsigned long long n);
void free(void *p);

int main(void) {
    int *p = (int *)malloc(sizeof(int) * 4);
    int sum;
    if (p == 0) {
        return 90;
    }

    p[0] = 3;
    p[1] = 7;
    2[p] = 11;
    if (p[2] != 11) {
        free(p);
        return 1;
    }

    sum = p[0] + 1[p] + p[2];
    free(p);
    if (sum != 21) {
        return 2;
    }
    return 0;
}
