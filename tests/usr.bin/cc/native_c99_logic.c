int bomb(void) {
    return 1 / 0;
}

int main(void) {
    int x = 0;

    x = x + (3 && 4);
    x = x + (0 || 5);
    x = x + (!0);
    x = x + (!7);

    if (0 && bomb()) {
        x = x + 100;
    }
    if (1 || bomb()) {
        x = x + 1;
    }

    return x - 4;
}
