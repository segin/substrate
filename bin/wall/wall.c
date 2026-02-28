#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char buf[1024];
    printf("\007Broadcast message from root...\n");
    while (fgets(buf, sizeof(buf), stdin)) {
        printf("%s", buf);
    }
    return 0;
}

