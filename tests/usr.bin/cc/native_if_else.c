int pick(int x) {
    if (x > 10) {
        return 1;
    } else {
        return 2;
    }
}

int main(void) {
    int a = pick(11);
    int b = pick(5);
    return (a - 1) + (b - 2);
}
