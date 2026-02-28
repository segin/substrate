#include <stdio.h>
#include <sys/stat.h> 

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    for(int i=1; i<argc; i++) {
        if(mkdir(argv[i], 0777) != 0) {
            printf("mkdir: cannot create directory '%s'\n", argv[i]);
        }
    }
    return 0;
}

