int main(void) {
    int x = 3;
    switch (x) {
    case 1 + 1:
        return 1;
    case (1 << 1) + 1:
        return 0;
    default:
        return 2;
    }
}
