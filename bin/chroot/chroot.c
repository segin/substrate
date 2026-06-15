/*
 * chroot — run a command (or an interactive shell) with a new root
 * directory.
 *
 *   chroot newroot [command [arg ...]]
 *
 * Changes the process root to newroot via chroot(2), moves the working
 * directory inside it (chdir("/")) so the old root can't be reached, then
 * execs the command.  With no command it runs ${SHELL:-/bin/sh} -i.
 * Requires the caller to be root (the chroot(2) syscall enforces this).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int usage(void) {
    fprintf(stderr, "usage: chroot newroot [command [arg ...]]\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return usage();
    }

    const char *newroot = argv[1];

    if (chroot(newroot) != 0) {
        fprintf(stderr, "chroot: %s: ", newroot);
        perror(NULL);
        return 1;
    }
    if (chdir("/") != 0) {
        perror("chroot: chdir(/)");
        return 1;
    }

    if (argc > 2) {
        execvp(argv[2], &argv[2]);
        fprintf(stderr, "chroot: %s: ", argv[2]);
        perror(NULL);
        return 127;
    }

    /* No command: drop into an interactive shell. */
    const char *sh = getenv("SHELL");
    if (!sh || !*sh) {
        sh = "/bin/sh";
    }
    char *sh_argv[] = { (char *)sh, (char *)"-i", NULL };
    execvp(sh, sh_argv);
    fprintf(stderr, "chroot: %s: ", sh);
    perror(NULL);
    return 127;
}
