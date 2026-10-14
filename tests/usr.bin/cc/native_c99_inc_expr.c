int main(void) {
    int x = 1;
    int a = x++;
    int b = x++;
    int c = ++x;

    if (a != 1) {
        return 1;
    }
    if (b != 2) {
        return 2;
    }
    if (c != 4) {
        return 3;
    }
    if (x != 4) {
        return 4;
    }
    return 0;
}
