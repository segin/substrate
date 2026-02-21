int main(void) {
    int i = 0;
    int s = 0;

    do {
        i = i + 1;
        if (i == 3) {
            continue;
        }
        s = s + i;
    } while (i < 5);

    return s - 12;
}
