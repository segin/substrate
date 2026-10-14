int sum2(int a[static 2], int b[const restrict static 2]) {
    return a[0] + b[1];
}

int main(void) {
    int x[2];
    int y[2];
    x[0] = 3;
    y[1] = 4;
    return sum2(x, y) - 7;
}
