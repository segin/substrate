/*
 * torture_commit.c -- strict memory-commit accounting (no overcommit).
 *
 * Exercises the POSIX-correct OOM behaviour the kernel now guarantees:
 *   a) Graceful OOM: a giant mmap/malloc returns MAP_FAILED/NULL + ENOMEM,
 *      it does NOT succeed-then-die on first touch.
 *   b) No false ENOMEM: a normal few-MB malloc that touches every page works.
 *   c) Fork COW correctness: parent's post-fork write is private to the parent.
 *   d) No accounting leak: many mmap/munmap + fork/exit cycles leave
 *      /proc/meminfo Committed_AS back at ~baseline.
 *   e) SIGBUS gone: an OOM does not raise SIGBUS.
 *
 * Output is line-buffered/unbuffered so the serial log captures progress.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

static volatile sig_atomic_t got_sigbus = 0;

static void sigbus_handler(int sig) {
    (void)sig;
    got_sigbus = 1;
}

/* Read the Committed_AS: kB value from /proc/meminfo (-1 if absent). */
static long read_committed_kb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[128];
    long val = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "Committed_AS:", 13) == 0) {
            val = strtol(line + 13, NULL, 10);
            break;
        }
    }
    fclose(f);
    return val;
}

static int test_graceful_oom(void) {
    printf("[a] graceful OOM (mmap > 512 MiB)...\n");
    /* Substrate is booted with -m 512M; ask for ~1 GiB anonymous private. */
    size_t huge = (size_t)1024 * 1024 * 1024;
    errno = 0;
    void *p = mmap(NULL, huge, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        printf("    mmap returned MAP_FAILED, errno=%d (%s)\n",
               errno, strerror(errno));
        if (errno == ENOMEM) {
            printf("    got NULL, handled\n");
            return 1;
        }
        printf("    FAIL: wrong errno (want ENOMEM)\n");
        return 0;
    }
    /* If we reach here mmap LIED -- touching it would historically SIGBUS. */
    printf("    FAIL: mmap succeeded (overcommit!) -- touching first page\n");
    *(volatile char *)p = 1; /* would SIGBUS/SIGSEGV under overcommit */
    munmap(p, huge);
    return 0;
}

static int test_graceful_oom_malloc(void) {
    printf("[a2] graceful OOM (malloc > 512 MiB)...\n");
    size_t req = (size_t)900 * 1024 * 1024;
    /* Show the direct mmap of the same size first (this is what malloc
     * does under the hood) so we can localise a discrepancy. */
    errno = 0;
    void *mp = mmap(NULL, req, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("    direct mmap(%zu) -> %p errno=%d\n", req, mp, errno);
    if (mp != MAP_FAILED) munmap(mp, req);

    errno = 0;
    void *p = malloc(req);
    printf("    malloc(%zu) -> %p errno=%d\n", req, p, errno);
    if (!p) {
        printf("    got NULL, handled\n");
        return 1;
    }
    printf("    FAIL: malloc succeeded (overcommit!) -- touching memory\n");
    memset(p, 0xAB, 4096);
    free(p);
    return 0;
}

static int test_no_false_enomem(void) {
    printf("[b] no false ENOMEM (8 MiB malloc, touch every page)...\n");
    size_t n = (size_t)8 * 1024 * 1024;
    unsigned char *p = malloc(n);
    if (!p) {
        printf("    FAIL: malloc(8 MiB) returned NULL under normal load\n");
        return 0;
    }
    for (size_t i = 0; i < n; i += 4096) {
        p[i] = (unsigned char)(i & 0xFF);
    }
    /* Verify a few pages held their values (no corruption). */
    int ok = 1;
    for (size_t i = 0; i < n; i += 4096) {
        if (p[i] != (unsigned char)(i & 0xFF)) { ok = 0; break; }
    }
    free(p);
    printf("    %s\n", ok ? "OK: all pages writable + readable" : "FAIL: corruption");
    return ok;
}

static int test_fork_cow(void) {
    printf("[c] fork COW correctness...\n");
    size_t n = 256 * 1024;
    unsigned char *p = mmap(NULL, n, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        printf("    FAIL: setup mmap failed\n");
        return 0;
    }
    memset(p, 'A', n);

    pid_t pid = fork();
    if (pid < 0) {
        printf("    FAIL: fork failed\n");
        munmap(p, n);
        return 0;
    }
    if (pid == 0) {
        /* Child: must still see 'A' even after the parent writes 'B'. */
        usleep(200 * 1000);
        int child_ok = 1;
        for (size_t i = 0; i < n; i++) {
            if (p[i] != 'A') { child_ok = 0; break; }
        }
        _exit(child_ok ? 0 : 1);
    }
    /* Parent: overwrite with 'B' while the child sleeps. */
    memset(p, 'B', n);
    int status = 0;
    waitpid(pid, &status, 0);
    int parent_ok = 1;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != 'B') { parent_ok = 0; break; }
    }
    munmap(p, n);
    int child_saw_A = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    printf("    parent sees B: %s, child saw original A: %s\n",
           parent_ok ? "yes" : "NO", child_saw_A ? "yes" : "NO");
    return parent_ok && child_saw_A;
}

static int test_no_leak(void) {
    printf("[d] no accounting leak (mmap/munmap + fork/exit cycles)...\n");
    long base = read_committed_kb();
    printf("    baseline Committed_AS = %ld kB\n", base);

    for (int i = 0; i < 200; i++) {
        size_t n = 1024 * 1024; /* 1 MiB */
        void *p = mmap(NULL, n, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) {
            *(volatile char *)p = 1; /* fault one page in */
            munmap(p, n);
        }
    }
    for (int i = 0; i < 40; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            void *p = mmap(NULL, 2 * 1024 * 1024, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p != MAP_FAILED) *(volatile char *)p = 1;
            _exit(0);
        } else if (pid > 0) {
            int st;
            waitpid(pid, &st, 0);
        }
    }

    long after = read_committed_kb();
    printf("    after cycles Committed_AS = %ld kB\n", after);
    if (base < 0 || after < 0) {
        printf("    FAIL: Committed_AS not exposed in /proc/meminfo\n");
        return 0;
    }
    /* Allow a small slack for steady-state daemon/shell heap churn. */
    long drift = after - base;
    if (drift < 0) drift = -drift;
    int ok = (drift <= 4096); /* <= 4 MiB slack */
    printf("    drift = %ld kB -> %s\n", drift, ok ? "OK (no leak)" : "FAIL (leak)");
    return ok;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGBUS, sigbus_handler);

    printf("=== torture_commit: strict commit accounting ===\n");

    int pass = 0, total = 0;

    total++; pass += test_no_false_enomem();
    total++; pass += test_fork_cow();
    total++; pass += test_graceful_oom();
    total++; pass += test_graceful_oom_malloc();
    total++; pass += test_no_leak();

    /* (e) SIGBUS gone: by here we have provoked OOM repeatedly. */
    printf("[e] SIGBUS check: got_sigbus = %d -> %s\n",
           (int)got_sigbus, got_sigbus ? "FAIL (SIGBUS raised!)" : "OK (no SIGBUS)");
    total++; pass += (got_sigbus == 0);

    printf("=== torture_commit: %d/%d passed ===\n", pass, total);
    printf("TORTURE_COMMIT_DONE rc=%d\n", (pass == total) ? 0 : 1);
    return (pass == total) ? 0 : 1;
}
