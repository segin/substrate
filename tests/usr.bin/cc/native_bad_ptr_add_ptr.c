int main(void) {
    int x = 1;
    int *p = &x;
    int *q = p + p;
    return q != 0;
}
