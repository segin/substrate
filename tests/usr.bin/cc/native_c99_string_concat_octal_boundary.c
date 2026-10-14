#include <string.h>

int main(void) {
    static const char s[] = "\0" "1\0";
    static const char *p = "\0" "1\0";
    if (sizeof s != 4) return 1;
    if (s[0] != 0 || s[1] != '1' || s[2] != 0 || s[3] != 0) return 2;
    if (memcmp(p, "\0" "1\0", 3) != 0) return 3;
    return 0;
}
