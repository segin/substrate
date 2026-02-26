#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    // pgrep pattern
    printf("pgrep: searching for %s...\n", argv[1]);
    return 0;
}

