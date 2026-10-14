int main(void) {
    int x = 2;

    x = x + 1;
    int y = 4;
    y = y + x;

    if (y != 7) {
        return 1;
    }
    return 0;
}
