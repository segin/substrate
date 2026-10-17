/* nullptr compares equal only with nullptr_t, a pointer or a null pointer constant (C23 6.5.10) */
int f(int x) { return x == nullptr; }
