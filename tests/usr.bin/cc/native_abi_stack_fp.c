double sum10(double a, double b, double c, double d, double e,
             double f, double g, double h, double i, double j) {
    double r = a + b;
    r = r + c;
    r = r + d;
    r = r + e;
    r = r + f;
    r = r + g;
    r = r + h;
    r = r + i;
    r = r + j;
    return r;
}

int main(void) {
    double v = sum10(1.0, 2.0, 3.0, 4.0, 5.0,
                     6.0, 7.0, 8.0, 9.0, 10.0);
    int iv = v;
    return iv - 55;
}
