double twice(double x) {
    double y = x + x;
    return y;
}

double mix(int a, double b) {
    double t = a + b;
    t = t * 2.0;
    return twice(t);
}

int main(void) {
    double r = mix(2, 1.5);
    int ir = r;
    return ir - 14;
}
