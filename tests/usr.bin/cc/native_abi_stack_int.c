int sum8(int a, int b, int c, int d, int e, int f, int g, int h) {
    int r = a + b;
    r = r + c;
    r = r + d;
    r = r + e;
    r = r + f;
    r = r + g;
    r = r + h;
    return r;
}

int main(void) {
    int v = sum8(1, 2, 3, 4, 5, 6, 7, 8);
    return v - 36;
}
