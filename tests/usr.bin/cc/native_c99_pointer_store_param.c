void setv(int *p, int v) {
    *p = v;
}

int main(void) {
    int x = 0;
    setv(&x, 4);
    return x - 4;
}
