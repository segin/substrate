int main(void) {
    int x = 1;
    asm volatile("" : "+r"(x));
    return x != 1;
}
