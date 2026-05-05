#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    char *user = "root";
    if (argc > 1) user = argv[1];
    
    // setuid(0); 
    // exec shell
    printf("su: switching to %s (mock)\n", user);
    char *shell_argv[] = {"sh", NULL};
    execvp("/bin/sh", shell_argv);
    perror("exec sh");
    return 1;
}

