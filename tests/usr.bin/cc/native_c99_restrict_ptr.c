int main(void) {
    int x = 4;
    int *restrict p = &x;
    return *p - 4;
}
