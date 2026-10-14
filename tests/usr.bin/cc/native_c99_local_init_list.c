int main(void) {
    int a[4] = {1, 2, 3};
    if (a[0] != 1 || a[1] != 2 || a[2] != 3) {
        return 1;
    }
    if (a[3] != 0) {
        return 2;
    }
    return 0;
}
