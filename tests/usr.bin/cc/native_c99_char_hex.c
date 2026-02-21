int main(void) {
    int a = 'A';
    int b = '\n';
    int c = 0x10 + 010 + 10;
    int d = '\\';
    int e = '\'';
    int f = '\x41';

    if (a != 65 || b != 10 || c != 34 || d != 92 || e != 39 || f != 65) {
        return 1;
    }
    return 0;
}
