#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: sleep seconds\n");
        return 1;
    }
    int s = atoi(argv[1]);
    sleep(s);
    return 0;
}

