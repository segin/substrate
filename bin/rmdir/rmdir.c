#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    for(int i=1; i<argc; i++) {
        if(rmdir(argv[i]) != 0) {
            printf("rmdir: failed to remove '%s'\n", argv[i]);
        }
    }
    return 0;
}

