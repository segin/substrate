/*
 * forkleak — processes that fork, exec, exit and are reaped give their
 * memory back.
 *
 * On x86_64 the pv (reverse-map) sanity check rejected every kernel
 * pointer (an i386 literal bound), so every pv entry ever made leaked:
 * about 40 KB per fork+exec+exit, never returned.
 *
 * Usage: forkleak [program to exec]   (default /bin/true)
 *
 * The parent first touches 4 MiB, and each child reads every page of it
 * before it execs (as a shell's child runs shell code first), mapping
 * ~1000 shared pages: one pv entry each.  Runs 50 rounds to warm caches,
 * then 300 more; MemFree must not drop by more than 2 MiB over the 300
 * (the leak lost ~10 MB).  On a busy host MemFree is too noisy to judge.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define WARMUP  50
#define ROUNDS  300
#define SLACK_KB 2048
#define BALLAST (4 * 1024 * 1024)

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

static char *ballast;

static int run(const char *prog)
{
    pid_t pid = fork();
    int status;

    if (pid == 0) {
        char *argv[] = { (char *)prog, NULL };
        volatile char sum = 0;

        for (size_t off = 0; off < BALLAST; off += 4096)
            sum += ballast[off];    /* map each shared page */
        (void)sum;
        char *envp[] = { NULL };
        execve(prog, argv, envp);
        _exit(127);
    }
    if (pid < 0 || waitpid(pid, &status, 0) != pid)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(int argc, char **argv)
{
    const char *prog = argc > 1 ? argv[1] : "/bin/true";
    long before, after;
    int i, bad = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    ballast = malloc(BALLAST);
    if (!ballast) {
        printf("FAIL  cannot allocate the ballast\n");
        return 1;
    }
    memset(ballast, 0x5A, BALLAST);     /* every page present, to share */
    for (i = 0; i < WARMUP; i++)
        if (run(prog) != 0)
            bad++;
    before = mem_free_kb();
    for (i = 0; i < ROUNDS; i++)
        if (run(prog) != 0)
            bad++;
    after = mem_free_kb();

    printf("%s %d rounds ran %s\n", bad ? "FAIL " : "ok   ",
           WARMUP + ROUNDS, bad ? "with failures" : "cleanly");
    printf("      MemFree %ld -> %ld kB over %d fork+exec+exit\n",
           before, after, ROUNDS);
    int leak = before >= 0 && after >= 0 && before - after > SLACK_KB;
    printf("%s memory comes back (within %d kB)\n", leak ? "FAIL " : "ok   ",
           SLACK_KB);
    printf("%s\n", bad || leak ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return bad || leak ? 1 : 0;
}
