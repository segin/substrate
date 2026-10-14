int main(void) {
    int x = 1;
    int *p = &x;
    return *(p + 1.0);
}
