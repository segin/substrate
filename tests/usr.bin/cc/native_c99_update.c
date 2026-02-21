int main(void) {
    int x = 10;

    x += 5;
    x -= 3;
    x *= 2;
    x /= 4;
    x %= 5;

    ++x;
    x--;

    for (int i = 0; i < 4; i++) {
        x += i;
    }

    return x - 7;
}
