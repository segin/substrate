#include <stdint.h>
#include <string.h>

int main(void) {
    uint32_t words[8];
    unsigned char *p;

    memset(words, 0, sizeof(words));
    p = (unsigned char *)words;
    p[3] = 0x11;
    ((unsigned char *)words)[4] = 0x22;
    *(&((unsigned char *)words)[5]) = 0x33;
    ((char *)words)[6] = 0x44;

    if (((unsigned char *)words)[3] != 0x11) return 1;
    if (((unsigned char *)words)[4] != 0x22) return 2;
    if (((unsigned char *)words)[5] != 0x33) return 3;
    if (((unsigned char *)words)[6] != 0x44) return 4;
    if (((unsigned char *)words)[12] != 0) return 5;
    if (((unsigned char *)words)[16] != 0) return 6;
    if (((unsigned char *)words)[20] != 0) return 7;
    if (((unsigned char *)words)[24] != 0) return 8;
    return 0;
}
