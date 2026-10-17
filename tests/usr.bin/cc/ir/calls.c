/* calls: direct, through a pointer, variadic, one whose result is unused */
int printf(const char *, ...);
static int twice(int x) { return x + x; }
int calls(int (*fp)(int), int v) {
    printf("%d\n", v);
    twice(v);
    return fp(v) + twice(v);
}
