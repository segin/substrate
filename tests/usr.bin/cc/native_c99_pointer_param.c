int readp(int *p) {
    return *p;
}

int main(void) {
    int x = 11;
    return readp(&x) - 11;
}
