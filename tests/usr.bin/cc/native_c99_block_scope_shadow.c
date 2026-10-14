int main(void) {
    int x = 1;

    {
        int x = 2;
        x = x + 1;
        if (x != 3) {
            return 1;
        }
    }
    if (x != 1) {
        return 2;
    }

    x = x + 4;
    {
        x = x + 1;
        int x = 9;
        if (x != 9) {
            return 3;
        }
    }

    if (x != 6) {
        return 4;
    }
    return 0;
}
