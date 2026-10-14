int main(void) {
    _Atomic(int) a = 1;
    _Atomic int b = 2;

    if (sizeof(a) != sizeof(int))
        return 1;
    if (sizeof(b) != sizeof(int))
        return 2;
    return ((int)a + (int)b == 3) ? 0 : 3;
}
