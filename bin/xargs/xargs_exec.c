/*
 * xargs_exec.c - invocation, prompting, tracing, parallelism for xargs.
 *
 * R-21..R-25, R-27..R-31: fork/execvp, -t trace, -p prompt (/dev/tty),
 * -o child-stdin-from-tty, -P parallel children, and the BSD/GNU exit-code
 * mapping (CR-4).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xargs.h"
#include <sys/wait.h>

static int running;     /* live -P children */

/* Print the command (space-joined) to stderr for -t / -p (R-22). */
static void trace_cmd(char **argv)
{
    for (int i = 0; argv[i]; i++)
        fprintf(stderr, "%s%s", i ? " " : "", argv[i]);
    fputc('\n', stderr);
}

/* -p: ask on /dev/tty, return 1 to run, 0 to skip (R-23). */
static int prompt_ok(void)
{
    FILE *tty = fopen("/dev/tty", "r");
    if (!tty)
        return 0;
    fputs("?...", stderr);
    fflush(stderr);
    int c = getc(tty);
    int first = c;
    while (c != EOF && c != '\n')
        c = getc(tty);
    fclose(tty);
    return (first == 'y' || first == 'Y');
}

/* Map one finished child's wait status into g_exit_status / g_stop (CR-4). */
static void map_status(int status)
{
    if (WIFSIGNALED(status)) {
        g_exit_status = 125;            /* killed by signal -> stop  */
        g_stop = 1;
        return;
    }
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0)
            return;
        if (code == 255) { g_exit_status = 124; g_stop = 1; return; }
        if (code == 127) { g_exit_status = 127; g_stop = 1; return; }
        if (code == 126) { g_exit_status = 126; g_stop = 1; return; }
        if (g_exit_status == 0)         /* 1-125: keep going, remember 123 */
            g_exit_status = 123;
    }
}

/* Reap exactly one child (blocking). */
static void reap_one(void)
{
    int status;
    pid_t pid = wait(&status);
    if (pid < 0)
        return;
    running--;
    map_status(status);
}

void xa_wait_all(struct xargs_opts *o)
{
    (void)o;
    while (running > 0)
        reap_one();
}

int xa_run(struct xargs_opts *o, char **argv, int argc)
{
    (void)argc;

    if (o->f_trace || o->f_prompt)
        trace_cmd(argv);
    if (o->f_prompt && !prompt_ok())
        return -1;                       /* declined */

    /* Throttle to -P concurrency before launching another (R-25). */
    long cap = (o->max_procs > 0) ? o->max_procs : 1;
    while (running >= cap)
        reap_one();

    pid_t pid = fork();
    if (pid < 0)
        xa_fatal("fork");

    if (pid == 0) {
        if (o->f_opentty) {              /* -o: child stdin from tty (R-24) */
            int fd = open("/dev/tty", O_RDONLY);
            if (fd >= 0) { dup2(fd, 0); if (fd != 0) close(fd); }
        }
        execvp(argv[0], argv);
        /* execvp failed (R-30): distinguish not-found vs not-executable. */
        fprintf(stderr, "%s: %s: %s\n", g_prog, argv[0], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    running++;
    if (cap <= 1)                        /* serial: reap immediately */
        reap_one();
    return 0;
}
