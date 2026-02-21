int main(void) {
    int sum = 0;

    for (int i = 0, j = 10; i < 3; i = i + 1, j = j + 2) {
        sum = sum + i + j;
    }

    if (sum != 39) {
        return 1;
    }
    return 0;
}
