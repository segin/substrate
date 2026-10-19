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
 * Usage: execfail <kind> <binary> [<binary to run after>]
 *
 * Each kind makes an exec of <binary> fail after its loader has built the
 * new address space:
 *
 *   x286-entry  copy of a Xenix/286 x.out, entry offset moved out of its
 *               code segment                                   (ENOEXEC)
 *   x386-eseg   copy of a Xenix/386 x.out, entry segment selector naming
 *               no segment                                     (ENOEXEC)
 *   truncate    copy of an ELKS (or other) program cut off part way
 *               through its text                               (ENOEXEC)
 *   env         <binary> itself, with a 12 MiB environment: the a.out and
 *               COFF loaders refuse it while building the stack  (E2BIG)
 *
 *   A. a child execs it: the expected error, its data, heap and stack
 *      intact; then, if given, it execs the third argument, which must
 *      exit 0;
 *   B. failed execs in a row return the same error and do not use up
 *      memory.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BAD_PATH        "/tmp/execfail.bad"
#define X286_ENTRY_OFF  24      /* struct xexec: x_entry */
#define X386_ESEG_OFF   72      /* struct xexec (32) + struct xext: xe_eseg */
#define TRUNC_LEN       40      /* 32-byte ELKS header + 8 bytes of text */
#define ENV_KIB         (12 * 1024)
#define VAR_LEN         1000

static int fails;
static char data_pattern[4096];
static const char *bad_path;
static char **bad_env;
static int want_errno;

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

/* Copy `src` to BAD_PATH, keep `len` bytes (0 = all), patch `patch_len`
 * bytes at `patch_off` with `patch`. */
static int make_copy(const char *src, size_t len, size_t patch_off,
                     const void *patch, size_t patch_len)
{
    static char buf[256 * 1024];
    int in, out;
    ssize_t n;

    in = open(src, O_RDONLY);
    if (in < 0)
        return -1;
    n = read(in, buf, sizeof(buf));
    close(in);
    if (n <= 0 || (patch_len && (size_t)n < patch_off + patch_len))
        return -1;
    if (len && (size_t)n > len)
        n = (ssize_t)len;
    if (patch_len)
        memcpy(buf + patch_off, patch, patch_len);
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

static char **make_env(int kib)
{
    int n = (int)((long)kib * 1024 / VAR_LEN) + 1;
    char **env = calloc((size_t)n + 1, sizeof(char *));
    char *block = malloc((size_t)n * VAR_LEN);

    if (!env || !block)
        return NULL;
    for (int i = 0; i < n; i++) {
        env[i] = block + (size_t)i * VAR_LEN;
        snprintf(env[i], VAR_LEN, "V%07d=", i);
        memset(env[i] + 9, 'x', VAR_LEN - 10);
        env[i][VAR_LEN - 1] = '\0';
    }
    return env;
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

static int exec_bad(void)
{
    char *argv[] = { (char *)bad_path, NULL };

    return execve(bad_path, argv, bad_env);
}

/* Child of A: exit 0 only if all is well (through `after`, if given). */
static void child_a(const char *after)
{
    char *after_argv[] = { "echo", "ok    good exec after the failed one",
                           NULL };
    char *envp[] = { NULL };
    unsigned char stack_pattern[2048];
    unsigned char *heap = malloc(8192);
    int rc, err;

    if (!heap)
        _exit(10);
    fill((unsigned char *)data_pattern, sizeof(data_pattern), 7);
    fill(heap, 8192, 31);
    fill(stack_pattern, sizeof(stack_pattern), 99);

    rc = exec_bad();
    err = errno;
    if (rc != -1 || err != want_errno) {
        printf("FAIL  bad exec gave %d errno %d, want errno %d\n", rc, err,
               want_errno);
        _exit(11);
    }
    if (!pattern_ok((unsigned char *)data_pattern, sizeof(data_pattern), 7) ||
        !pattern_ok(heap, 8192, 31) ||
        !pattern_ok(stack_pattern, sizeof(stack_pattern), 99)) {
        printf("FAIL  memory changed across the failed exec\n");
        _exit(12);
    }
    printf("ok    child: errno %d, memory intact\n", err);
    if (!after)
        _exit(0);
    execve(after, after_argv, envp);
    printf("FAIL  good exec after a failed one: errno %d\n", errno);
    _exit(13);
}

int main(int argc, char **argv)
{
    const char *kind, *bin, *after;
    static char *empty_env[] = { NULL };
    int status = 0, rounds = 300, rc = 0;
    pid_t pid;
    long before, after_kb;
    int i, wrong = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) {
        fprintf(stderr, "usage: execfail x286-entry|x386-eseg|truncate|env "
                        "<binary> [<binary to run after>]\n");
        return 2;
    }
    kind = argv[1];
    bin = argv[2];
    after = argc > 3 ? argv[3] : NULL;
    bad_path = BAD_PATH;
    bad_env = empty_env;
    want_errno = ENOEXEC;

    if (strcmp(kind, "x286-entry") == 0) {
        unsigned int entry = 0x20000;   /* past any 64 KiB segment */
        rc = make_copy(bin, 0, X286_ENTRY_OFF, &entry, sizeof(entry));
    } else if (strcmp(kind, "x386-eseg") == 0) {
        unsigned short eseg = 0xfff8;   /* no segment has it */
        rc = make_copy(bin, 0, X386_ESEG_OFF, &eseg, sizeof(eseg));
    } else if (strcmp(kind, "truncate") == 0) {
        rc = make_copy(bin, TRUNC_LEN, 0, NULL, 0);
    } else if (strcmp(kind, "env") == 0) {
        bad_path = bin;
        bad_env = make_env(ENV_KIB);
        want_errno = E2BIG;
        rounds = 20;    /* each one copies 12 MiB in */
        if (!bad_env)
            rc = -1;
    } else {
        fprintf(stderr, "execfail: unknown kind %s\n", kind);
        return 2;
    }
    if (rc != 0) {
        printf("FAIL  cannot prepare %s from %s: errno %d\n", kind, bin, errno);
        return 1;
    }

    /* A */
    pid = fork();
    if (pid == 0)
        child_a(after);
    check(pid > 0 && waitpid(pid, &status, 0) == pid, "A: child reaped");
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          after ? "A: failed exec returned, then the good binary ran"
                : "A: failed exec returned to an intact caller");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        printf("      status 0x%x\n", status);

    /* B: the failures happen in this process, which must keep running. */
    before = mem_free_kb();
    for (i = 0; i < rounds; i++)
        if (exec_bad() != -1 || errno != want_errno)
            wrong++;
    after_kb = mem_free_kb();
    check(wrong == 0, "B: every failed exec returned the same error");
    printf("      MemFree %ld -> %ld kB over %d failed execs\n",
           before, after_kb, rounds);
    check(before < 0 || after_kb < 0 || before - after_kb < 2048,
          "B: failed execs do not leak memory");

    unlink(BAD_PATH);
    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails ? 1 : 0;
}
