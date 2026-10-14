int main(void) {
    int x = 1;
    double y = 2.0;
    int *p = &x;
    double *q = &y;
    return p < q;
}
