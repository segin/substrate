#include <stdio.h>
#include <unistd.h>
#include <grp.h>

int main(int argc, char *argv[]) {
    (void)argv;
    if (argc < 2) return 1;
    // setgid(getgrnam(argv[1])->gr_gid);
    // execvp(shell, ...);
    printf("newgrp: not fully implemented\n");
    return 0;
}

