#include <stdio.h>

static int get_ifs() {
    return 123;
}

int main(void) {
    int v = get_ifs();
    if (v != 123) {
        return 1;
    }
    puts("ok");
    return 0;
}
