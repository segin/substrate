int main(void) {
    int a = 1, b = 2;
    int *p = &a, **pp = &p;
    int x = 0, y = 0, z = 0;

    x = **pp;
    y = b;
    z = x + y;

    if (x != 1) {
        return 1;
    }
    if (y != 2) {
        return 2;
    }
    if (z != 3) {
        return 3;
    }
    return 0;
}
