#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern int setpriority(int which, int who, int prio);

int main(int argc, char *argv[]) {
    int inc = 10;
    int i = 1;
    if (argc > 1 && argv[1][0] == '-') {
        inc = atoi(argv[1] + 1);
        i++;
    }
    (void)inc;
    
    if (i >= argc) {
        printf("usage: nice [-n] command [args]\n");
        return 1;
    }

    // setpriority(PRIO_PROCESS, 0, inc);
    execvp(argv[i], &argv[i]);
    perror(argv[i]);
    return 1;
}
