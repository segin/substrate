#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    (void)argv;
    if (argc < 3) return 1;
    // chmod <mode> <file>
    // Needs strtol for octal
    // Needs chmod syscall wrapper
    printf("chmod: not implemented\n");
    return 0;
}

