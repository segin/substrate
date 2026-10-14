int main(void) {
    int x = 0;

    goto mid;
    x = 99;

mid:
    x = x + 2;
    if (x < 5) {
        goto mid2;
    }
    return 1;

mid2:
    x = x + 3;
    if (x == 5) {
        goto done;
    }
    return 2;

done:
    return 0;
}
