static char buf[8] = "ab";

int main(void) {
    if (buf[0] != 'a' || buf[1] != 'b' || buf[2] != 0) {
        return 1;
    }
    buf[0] = 'x';
    return buf[0] == 'x' ? 0 : 2;
}
