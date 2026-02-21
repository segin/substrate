int abs1(int x) {
    if (x < 0) {
        return 0 - x;
    }
    return x;
}

int main(void) {
    return abs1(-7) - 7;
}
