int sum(int a[]);
int sum(int *a) {
    return a[0] + a[1];
}

int main(void) {
    int v[2];
    v[0] = 2;
    v[1] = 5;
    return sum(v) - 7;
}
