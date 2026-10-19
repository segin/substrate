/*
 * aoutrun — a Linux ZMAGIC a.out, whose text starts at address 0, runs.
 *
 * The a.out loader read the image into place through the user address,
 * and to the read path a buffer at address 0 is no buffer: the first page
 * of the text stayed zero-filled while the read reported success, and the
 * program ran on through the zeros into its data, where it hung or
 * crashed.  No Slackware program ran.
 *
 * Usage: aoutrun <Linux a.out printenv>
 *   e.g. aoutrun /perso/linux/usr/bin/printenv   (Slackware 1.1.2)
 *
 * Runs `printenv AOUTRUN` with AOUTRUN set; it must print the value and
 * exit 0 within 10 seconds.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define VALUE "the-text-page-at-address-zero-was-loaded"

int main(int argc, char **argv)
{
    char out[256];
    int pfd[2], status = 0, fails = 0;
    size_t got = 0;
    ssize_t n;
    pid_t pid;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: aoutrun <a.out printenv>\n");
        return 2;
    }
    if (pipe(pfd) != 0)
        return 1;
    pid = fork();
    if (pid == 0) {
        char *cargv[] = { argv[1], "AOUTRUN", NULL };
        char *cenvp[] = { "AOUTRUN=" VALUE, NULL };

        close(pfd[0]);
        dup2(pfd[1], 1);
        alarm(10);      /* a hang shows as SIGALRM */
        execve(argv[1], cargv, cenvp);
        _exit(127);
    }
    close(pfd[1]);
    while (got + 1 < sizeof(out) &&
           (n = read(pfd[0], out + got, sizeof(out) - 1 - got)) > 0)
        got += (size_t)n;
    out[got] = '\0';
    out[strcspn(out, "\n")] = '\0';
    close(pfd[0]);
    if (pid < 0 || waitpid(pid, &status, 0) != pid)
        return 1;

    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        printf("FAIL  printenv: status 0x%x%s\n", status,
               WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM
                   ? " (hung)" : "");
        fails++;
    } else {
        printf("ok    printenv exited 0\n");
    }
    if (strcmp(out, VALUE) != 0) {
        printf("FAIL  printenv printed \"%s\"\n", out);
        fails++;
    } else {
        printf("ok    printenv printed the value\n");
    }
    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails ? 1 : 0;
}
