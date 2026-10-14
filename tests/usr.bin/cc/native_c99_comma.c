int main(void) {
    int x = 0;
    int y = (x = 1, x + 2);
    int z = 0;

    z = (z = 4, z = z + 1, z);

    if (x != 1) {
        return 1;
    }

    return y + z - 8;
}
