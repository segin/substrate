int main(void) {
    int x = 0;
    x += (1 ? 5 : 9);
    x += (0 ? 2 : 3);
    x += ((x > 0) ? 1 : 2);
    x += (0 ? (x = 99) : 0);
    return x - 9;
}
