#include <stddef.h>
#include <stdio.h>

int main(void) {
    size_t width = 6;
    size_t fraction_len = 0;
    int flag = 1;

    width += (fraction_len == 0 ? -1 : flag);
    if (width != 5) {
        printf("width=%zu\n", width);
        return 1;
    }
    return 0;
}
