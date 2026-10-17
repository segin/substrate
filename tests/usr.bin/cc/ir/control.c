/* control flow: a loop, a conditional expression, a switch */
int sum(int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += i;
    return s > 100 ? 100 : s;
}
int pick(int k) {
    switch (k) {
    case 1: return 10;
    case 2: return 20;
    default: return -1;
    }
}
