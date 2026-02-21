int ping(int x) {
    return x + 1;
}

int main(void) {
    int a = 0;
    ping(a);
    a = ping(4);
    return a - 5;
}
