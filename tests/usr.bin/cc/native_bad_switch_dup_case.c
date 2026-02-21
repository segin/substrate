int main(void) {
    int x = 0;
    switch (x) {
    case 1:
        return 0;
    case 1 + 0:
        return 1;
    default:
        return 2;
    }
}
