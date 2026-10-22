int main(void) {
    /* variables of the literals' types: a compare of constants is folded at compile time */
    unsigned one = 1u;
    unsigned long long seven = 7ull;

    if (!((one - 2u) > 3u)) {
        return 1;
    }
    if (!((seven - 9ull) > 10ull)) {
        return 2;
    }
    if ((8u >> one) != 4u) {
        return 3;
    }
    return 0;
}
