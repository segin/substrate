#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    printf("Message from root to %s...\n", argv[1]);
    char buf[1024];
    while (fgets(buf, sizeof(buf), stdin)) {
        // Send to tty of user
    }
    return 0;
}

