#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    if (argc < 2) return 0;
    
    time_t start = time(NULL);
    
    int pid = fork();
    if (pid == 0) {
        execvp(argv[1], &argv[1]);
        perror("exec");
        exit(1);
    } else {
        waitpid(pid, NULL, 0);
        time_t end = time(NULL);
        printf("\nreal %lld\n", (long long)(end - start));
    }
    return 0;
}

