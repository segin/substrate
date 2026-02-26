int main(void) {
    int a = L'\n';
    int b = U'\x41';
    const char *s = L"ab" "cd";
    const char *t = u8"xy";

    if (a != 10 || b != 65) {
        return 1;
    }
    if (s[0] != 'a' || s[3] != 'd') {
        return 2;
    }
    if (t[0] != 'x' || t[1] != 'y') {
        return 3;
    }
    return 0;
}
