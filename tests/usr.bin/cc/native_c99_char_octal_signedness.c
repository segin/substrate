#include <stdio.h>

int main(void) {
    char c = '\247';
    if (c != '\247') {
        return 1;
    }
    if ((int)c != -89) {
        return 2;
    }
    if ((unsigned char)c != 167u) {
        return 3;
    }
    puts("ok");
    return 0;
}
