/*
 * execfail — an exec that fails part way through loading returns to the
 * caller, whose address space is still there.
 *
 * The a.out, COFF, ELKS and x.out loaders made the new address space the
 * process's -- and some destroyed the old one -- before the steps that can
 * still fail (reading the image, building the stack, installing the LDT).
 * Such a failure came back to a caller running on a half-built address
 * space: it died on its first instruction, or ran on, leaking the new one.
 *
 * Usage: execfail <good x.out (Xenix/286) binary>
 *
 * The good binary is copied with its entry offset moved out of the code
 * segment.  Its header still passes the early checks, so the loader builds
 * the new address space and loads the segments before it notices.
 *
 *   A. a child execs the bad copy: ENOEXEC, its memory (data, heap,
 *      stack) intact, and it can then exec the good binary, which exits 0;
 *   B. 300 failed execs in a row do not use up memory.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BAD_PATH   "/tmp/execfail.bad"
#define ENTRY_OFF  24           /* struct xexec: x_entry */
#define ROUNDS     300

static int fails;
static char data_pattern[4096];

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static long mem_free_kb(void)
{
    char line[128];
    long kb = -1;
    FILE *f = fopen("/proc/meminfo", "r");

    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "MemFree: %ld", &kb) == 1)
            break;
    fclose(f);
    return kb;
}

static int make_bad_copy(const char *good)
{
    static char buf[256 * 1024];
    int in, out;
    ssize_t n;
    unsigned int entry = 0x20000;   /* past any 64 KiB segment */

    in = open(good, O_RDONLY);
    if (in < 0)
        return -1;
    n = read(in, buf, sizeof(buf));
    close(in);
    if (n <= ENTRY_OFF + 4)
        return -1;
    memcpy(buf + ENTRY_OFF, &entry, 4);
    out = open(BAD_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (out < 0)
        return -1;
    if (write(out, buf, (size_t)n) != n) {
        close(out);
        return -1;
    }
    close(out);
    return chmod(BAD_PATH, 0755);
}

static int pattern_ok(const unsigned char *p, size_t len, unsigned char seed)
{
    for (size_t i = 0; i < len; i++)
        if (p[i] != (unsigned char)(seed + i))
            return 0;
    return 1;
}

static void fill(unsigned char *p, size_t len, unsigned char seed)
{
    for (size_t i = 0; i < len; i++)
        p[i] = (unsigned char)(seed + i);
}

/* Child of A: exit 0 only through the good binary. */
static void child_a(const char *good)
{
    char *bad_argv[] = { "bad", NULL };
    char *good_argv[] = { "echo", "ok    good exec after the failed one", NULL };
    char *envp[] = { NULL };
    unsigned char stack_pattern[2048];
    unsigned char *heap = malloc(8192);
    int rc, err;

    if (!heap)
        _exit(10);
    fill((unsigned char *)data_pattern, sizeof(data_pattern), 7);
    fill(heap, 8192, 31);
    fill(stack_pattern, sizeof(stack_pattern), 99);

    rc = execve(BAD_PATH, bad_argv, envp);
    err = errno;
    if (rc != -1 || err != ENOEXEC) {
        printf("FAIL  bad exec gave %d errno %d\n", rc, err);
        _exit(11);
    }
    if (!pattern_ok((unsigned char *)data_pattern, sizeof(data_pattern), 7) ||
        !pattern_ok(heap, 8192, 31) ||
        !pattern_ok(stack_pattern, sizeof(stack_pattern), 99)) {
        printf("FAIL  memory changed across the failed exec\n");
        _exit(12);
    }
    printf("ok    child: ENOEXEC, memory intact\n");
    fflush(stdout);
    execve(good, good_argv, envp);
    printf("FAIL  good exec after a failed one: errno %d\n", errno);
    _exit(13);
}

int main(int argc, char **argv)
{
    const char *good;
    int status = 0;
    pid_t pid;
    long before, after;
    int i, wrong = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: execfail <good x.out binary>\n");
        return 2;
    }
    good = argv[1];
    if (make_bad_copy(good) != 0) {
        printf("FAIL  cannot make the bad copy of %s: errno %d\n", good, errno);
        return 1;
    }

    /* A */
    pid = fork();
    if (pid == 0)
        child_a(good);
    check(pid > 0 && waitpid(pid, &status, 0) == pid, "A: child reaped");
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "A: failed exec returned, then the good binary ran and exited 0");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        printf("      status 0x%x\n", status);

    /* B: the failures happen in this process, which must keep running. */
    before = mem_free_kb();
    for (i = 0; i < ROUNDS; i++) {
        char *bad_argv[] = { "bad", NULL };
        char *envp[] = { NULL };

        if (execve(BAD_PATH, bad_argv, envp) != -1 || errno != ENOEXEC)
            wrong++;
    }
    after = mem_free_kb();
    check(wrong == 0, "B: every failed exec returned ENOEXEC");
    printf("      MemFree %ld -> %ld kB over %d failed execs\n",
           before, after, ROUNDS);
    check(before < 0 || after < 0 || before - after < 2048,
          "B: failed execs do not leak memory");

    unlink(BAD_PATH);
    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails ? 1 : 0;
}
