int vlog(int a, int b, int c, int d, int e, int f, int g, ...) {
    return g;
}

int main(void) {
    int x = vlog(1, 2, 3, 4, 5, 6, 7, 8.0, 9.0);
    return x - 7;
}
