/* GNU C: sizeof(void) is 1, in constant expressions as well */
enum { VOID_SIZE = sizeof(void) };
static char buf[sizeof(void) + 1];

int main(void) {
    int n = sizeof(void);
    return n + VOID_SIZE + (int)sizeof buf - 4;
}
