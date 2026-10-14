#include <string.h>

int main(void) {
    const char *s = "hello, " "world";
    return strcmp(s, "hello, world") == 0 ? 0 : 1;
}
