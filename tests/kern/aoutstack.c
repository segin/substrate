/*
 * aoutstack — the a.out initial stack is sized to its arguments, and an
 * exec whose arguments are too large fails with E2BIG instead of writing
 * past the stack.
 *
 * The a.out and COFF loaders mapped 128 KiB of stack and copied argv/envp
 * into it with no bound.  Beyond the mapping the copy leaned on the stack-
 * growth fault handler, and beyond the 8 MiB stack region the kernel
 * faulted in the copy.
 *
 * Usage: aoutstack <a.out or COFF echo>
 *   e.g. aoutstack /perso/svr3/bin/echo   (SVR3 COFF: the same stack code)
 *
 *   A. a 12 MiB environment: execve fails with E2BIG, and the caller
 *      carries on;
 *   B. a 1 MiB environment (more than the 128 KiB mapped before): echo
 *      runs, prints its argument intact, and exits 0.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define VAR_LEN 1000

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

/* Variable i: "V%07d=" then 'a' + i % 26 repeated, VAR_LEN bytes in all. */
static void make_var(char *buf, int i)
{
    snprintf(buf, VAR_LEN, "V%07d=", i);
    memset(buf + 9, 'a' + i % 26, VAR_LEN - 10);
    buf[VAR_LEN - 1] = '\0';
}

/* An environment of `kib` KiB, in VAR_LEN-byte variables; *n_out = count. */
static char **make_env(int kib, int *n_out)
{
    int n = (int)((long)kib * 1024 / VAR_LEN) + 1;
    char **env = calloc((size_t)n + 1, sizeof(char *));
    char *block = malloc((size_t)n * VAR_LEN);

    if (!env || !block)
        return NULL;
    for (int i = 0; i < n; i++) {
        env[i] = block + (size_t)i * VAR_LEN;
        make_var(env[i], i);
    }
    *n_out = n;
    return env;
}

/*
 * Exec `prog name` with `env` in a child, its stdout into `out`.  Returns
 * the wait status (-1 on a harness failure); *exec_errno is the errno of
 * a failed execve, else 0.
 */
static int run(const char *prog, const char *name, char **env,
               int *exec_errno, char *out, size_t outsz)
{
    int epipe[2], opipe[2], status;
    size_t got = 0;
    ssize_t n;
    pid_t pid;

    if (pipe(epipe) != 0 || pipe(opipe) != 0)
        return -1;
    pid = fork();
    if (pid == 0) {
        char *argv[] = { (char *)prog, (char *)name, NULL };
        int err;

        close(epipe[0]);
        close(opipe[0]);
        dup2(opipe[1], 1);
        fcntl(epipe[1], F_SETFD, FD_CLOEXEC);
        execve(prog, argv, env);
        err = errno;
        if (write(epipe[1], &err, sizeof(err)) != (ssize_t)sizeof(err))
            _exit(98);
        _exit(99);
    }
    close(epipe[1]);
    close(opipe[1]);
    while (got + 1 < outsz &&
           (n = read(opipe[0], out + got, outsz - 1 - got)) > 0)
        got += (size_t)n;
    out[got] = '\0';
    close(opipe[0]);
    *exec_errno = 0;
    if (read(epipe[0], exec_errno, sizeof(*exec_errno)) != sizeof(*exec_errno))
        *exec_errno = 0;
    close(epipe[0]);
    if (pid < 0 || waitpid(pid, &status, 0) != pid)
        return -1;
    return status;
}

int main(int argc, char **argv)
{
    static char out[2 * VAR_LEN];
    const char *word = "argument-after-a-megabyte-of-environment";
    char **big, **fits;
    int nbig, nfits, status, err;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: aoutstack <a.out or COFF echo>\n");
        return 2;
    }
    big = make_env(12 * 1024, &nbig);
    fits = make_env(1024, &nfits);
    if (!big || !fits) {
        printf("FAIL  out of memory\n");
        return 1;
    }

    /* A */
    status = run(argv[1], "V0000000", big, &err, out, sizeof(out));
    check(status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 99 &&
          err == E2BIG, "A: 12 MiB environment: E2BIG, caller carries on");
    if (err != E2BIG)
        printf("      status 0x%x errno %d\n", status, err);

    /* B */
    (void)nfits;
    status = run(argv[1], word, fits, &err, out, sizeof(out));
    check(status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "B: 1 MiB environment: the program runs and exits 0");
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        printf("      status 0x%x errno %d\n", status, err);
    out[strcspn(out, "\n")] = '\0';
    check(strcmp(out, word) == 0, "B: its argument arrives intact");
    if (strcmp(out, word) != 0)
        printf("      got \"%s\"\n", out);

    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails ? 1 : 0;
}
