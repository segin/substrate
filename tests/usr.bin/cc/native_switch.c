int score(int x) {
    int out = 0;

    switch (x) {
    case 0:
        out = 10;
        break;
    case 1:
    case 2:
        out = 20;
        break;
    default:
        out = 30;
        break;
    }

    return out;
}

int main(void) {
    int a = score(0);
    int b = score(2);
    int c = score(9);
    return (a - 10) + (b - 20) + (c - 30);
}
