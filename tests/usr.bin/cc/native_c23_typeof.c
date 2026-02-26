int main(void) {
    int x = 7;
    typeof(x) y = 3;
    typeof_unqual(y) z = 4;
    return (y + z == 7) ? 0 : 1;
}
