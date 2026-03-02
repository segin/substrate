inline int add_local(int a, int b) {
    return a + b;
}

extern inline int add_ext(int a, int b) {
    return a + b + 1;
}

int main(void) {
    return add_local(2, 3) + add_ext(2, 3) - 11;
}
