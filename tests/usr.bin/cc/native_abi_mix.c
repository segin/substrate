double mix10(int a, int b, int c, int d, int e, int f, int g,
             double x, double y, double z) {
    double s = g + x;
    s = s + y;
    s = s + z;
    return s;
}

int main(void) {
    double r = mix10(1, 2, 3, 4, 5, 6, 7, 1.5, 2.5, 3.0);
    int i = r;
    return i - 14;
}
