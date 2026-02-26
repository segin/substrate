#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: kill [-sig] pid...\n");
        return 1;
    }

    int sig = SIGTERM;
    int i = 1;
    if (argv[1][0] == '-') {
        sig = atoi(argv[1] + 1);
        i++;
    }

    for (; i < argc; i++) {
        pid_t pid = atoi(argv[i]);
        if (kill(pid, sig) < 0) {
            perror(argv[i]);
        }
    }
    return 0;
}