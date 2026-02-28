#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    
    // test -f file
    if (strcmp(argv[1], "-f") == 0 && argc > 2) {
        struct stat st;
        if (stat(argv[2], &st) == 0 && S_ISREG(st.st_mode)) return 0;
        return 1;
    }
    // test -d dir
    if (strcmp(argv[1], "-d") == 0 && argc > 2) {
        struct stat st;
        if (stat(argv[2], &st) == 0 && S_ISDIR(st.st_mode)) return 0;
        return 1;
    }
    // test -e file
    if (strcmp(argv[1], "-e") == 0 && argc > 2) {
        if (access(argv[2], F_OK) == 0) return 0;
        return 1;
    }
    
    return 1;
}
