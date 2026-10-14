int main(void) {
    if (!((1u - 2u) > 3u)) {
        return 1;
    }
    if (!((7ull - 9ull) > 10ull)) {
        return 2;
    }
    if ((8u >> 1u) != 4u) {
        return 3;
    }
    return 0;
}
