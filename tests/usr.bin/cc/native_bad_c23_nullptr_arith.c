/* nullptr_t is not an arithmetic type (C23 6.5.7) */
int f(int x) { return x + nullptr; }
