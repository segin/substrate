int add(int a, int b) {
    int c = a + b;
    return c;
}

int twice(int x) {
    x = x + x;
    return x;
}

int main(void) {
    int v = add(2, 3);
    v = twice(v);
    return v - 10;
}
