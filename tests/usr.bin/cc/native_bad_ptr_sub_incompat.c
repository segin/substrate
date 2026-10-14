int main(void) {
    int x = 0;
    char y = 0;
    int *pi = &x;
    char *pc = &y;
    return (int)(pi - pc);
}
