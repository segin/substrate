#include <stdlib.h>

static void cleanup(void) {
}

int main(void) {
    return atexit(cleanup) != 0;
}
