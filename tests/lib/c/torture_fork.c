/*
 * torture_fork.c — fork() address-space semantics torture test.
 *
 * Built to chase the nginx-worker failure: nginx's master mmap()s
 * MAP_SHARED|MAP_ANON shared-memory zones (slab, accept mutex), then
 * fork()s a worker that must keep sharing those pages with the
 * master.  On substrate the worker never reached its accept() loop;
 * the suspect is pmap_fork() COW-stripping *every* page, including
 * MAP_SHARED ones, so the first post-fork write silently breaks the
 * sharing into a private copy.
 *
 * Scenarios (each prints PASS/FAIL; exit code = number of failures):
 *   sc1  MAP_SHARED|MAP_ANON stays shared across fork (child write
 *        visible to parent)                              <- the bug
 *   sc2  MAP_PRIVATE is COW across fork (child write NOT visible)
 *   sc3  shared counter hammered by parent + child (slab/mutex shape)
 *   sc4  many inherited fds usable in the child
 *   sc5  worker-shape: heavy parent state, child reaches a work loop
 *        and reports liveness through a shared flag
 *
 * Portable: runs on Linux/BSD/macOS host as a baseline and on
 * substrate as the target.  build host: cc -o torture_fork torture_fork.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

static int failures = 0;

static void ok(const char *name, int pass, const char *detail) {
    printf("  %-26s %s%s%s\n", name, pass ? "PASS" : "FAIL",
           detail && *detail ? " — " : "", detail ? detail : "");
    if (!pass) failures++;
}

/* sc1: MAP_SHARED|MAP_ANON must stay shared across fork. */
static void sc1_shared_anon(void) {
    printf("sc1: MAP_SHARED|MAP_ANON shared across fork\n");
    volatile int *shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANON, -1, 0);
    if (shm == MAP_FAILED) { ok("mmap", 0, strerror(errno)); return; }

    shm[0] = 0x11111111;          /* parent sentinel before fork  */
    pid_t pid = fork();
    if (pid < 0) { ok("fork", 0, strerror(errno)); return; }
    if (pid == 0) {
        shm[0] = 0x22222222;      /* child writes the shared page  */
        shm[1] = 0xC0FFEE;
        _exit(0);
    }
    int st; waitpid(pid, &st, 0);
    char d[64];
    snprintf(d, sizeof(d), "parent sees 0x%x / 0x%x", shm[0], shm[1]);
    /* If sharing is intact, parent observes the child's writes. */
    ok("child write visible", shm[0] == 0x22222222 && shm[1] == 0xC0FFEE, d);
    munmap((void *)shm, 4096);
}

/* sc2: MAP_PRIVATE must be COW — child writes invisible to parent. */
static void sc2_private_cow(void) {
    printf("sc2: MAP_PRIVATE COW across fork\n");
    volatile int *pm = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANON, -1, 0);
    if (pm == MAP_FAILED) { ok("mmap", 0, strerror(errno)); return; }
    pm[0] = 0xAAAA;
    pid_t pid = fork();
    if (pid < 0) { ok("fork", 0, strerror(errno)); return; }
    if (pid == 0) { pm[0] = 0xBBBB; _exit(0); }
    int st; waitpid(pid, &st, 0);
    char d[64]; snprintf(d, sizeof(d), "parent still sees 0x%x", pm[0]);
    ok("child write isolated", pm[0] == 0xAAAA, d);
    munmap((void *)pm, 4096);
}

/* sc3: parent + child both hammer a shared counter (slab/mutex shape).
 * Final value must equal both contributions — proves continued sharing
 * through many faults, not just the first write. */
static void sc3_shared_counter(void) {
    printf("sc3: shared counter hammered by both sides\n");
    volatile int *c = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANON, -1, 0);
    if (c == MAP_FAILED) { ok("mmap", 0, strerror(errno)); return; }
    c[0] = 0;
    const int N = 10000;
    pid_t pid = fork();
    if (pid < 0) { ok("fork", 0, strerror(errno)); return; }
    if (pid == 0) {
        for (int i = 0; i < N; i++) c[0] = c[0] + 1;
        _exit(0);
    }
    /* race is fine — we only check the page is genuinely shared, i.e.
     * the final value is strictly greater than either side alone could
     * produce if it had a private copy (which would cap at N). */
    for (int i = 0; i < N; i++) c[0] = c[0] + 1;
    int st; waitpid(pid, &st, 0);
    char d[64]; snprintf(d, sizeof(d), "final=%d (private copy would be <=%d)", c[0], N);
    ok("both sides shared one page", c[0] > N, d);
    munmap((void *)c, 4096);
}

/* sc4: many inherited fds usable in the child. */
static void sc4_inherited_fds(void) {
    printf("sc4: inherited fds usable in child\n");
    enum { NF = 12 };
    int fds[NF];
    int opened = 0;
    for (int i = 0; i < NF; i++) {
        fds[i] = open("/dev/null", O_RDWR);
        if (fds[i] >= 0) opened++;
    }
    volatile int *flag = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANON, -1, 0);
    if (flag == MAP_FAILED) { ok("mmap", 0, strerror(errno)); return; }
    flag[0] = 0;
    pid_t pid = fork();
    if (pid < 0) { ok("fork", 0, strerror(errno)); return; }
    if (pid == 0) {
        int good = 0;
        for (int i = 0; i < NF; i++)
            if (fds[i] >= 0 && write(fds[i], "x", 1) == 1) good++;
        flag[0] = good;
        _exit(0);
    }
    int st; waitpid(pid, &st, 0);
    char d[64]; snprintf(d, sizeof(d), "child wrote %d/%d inherited fds", flag[0], opened);
    ok("inherited fds work", flag[0] == opened && opened > 0, d);
    for (int i = 0; i < NF; i++) if (fds[i] >= 0) close(fds[i]);
    munmap((void *)flag, 4096);
}

static volatile sig_atomic_t got_sig = 0;
static void onsig(int s) { (void)s; got_sig = 1; }

/* sc5: worker-shape. Parent installs handlers, opens fds, maps a
 * shared "liveness" region, forks. The child must run a real loop and
 * publish progress into the shared region — mirroring an nginx worker
 * reaching its event loop. */
static void sc5_worker_shape(void) {
    printf("sc5: worker-shape child reaches its loop\n");
    signal(SIGUSR1, onsig);                       /* heavy-ish state */
    int junk[8];
    for (int i = 0; i < 8; i++) junk[i] = open("/dev/null", O_RDWR);
    volatile int *live = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANON, -1, 0);
    if (live == MAP_FAILED) { ok("mmap", 0, strerror(errno)); return; }
    live[0] = 0;          /* heartbeat the worker increments */
    live[1] = 0;          /* set to 0xD09E when worker finishes */

    pid_t pid = fork();
    if (pid < 0) { ok("fork", 0, strerror(errno)); return; }
    if (pid == 0) {
        /* "worker": loop, publishing a heartbeat into shared mem. */
        for (int i = 0; i < 1000; i++) live[0] = i + 1;
        live[1] = 0xD09E;
        _exit(0);
    }
    int st; waitpid(pid, &st, 0);
    int exited_ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    char d[80];
    snprintf(d, sizeof(d), "exit_ok=%d heartbeat=%d done=0x%x",
             exited_ok, live[0], live[1]);
    ok("worker ran + published", exited_ok && live[0] == 1000 && live[1] == 0xD09E, d);
    for (int i = 0; i < 8; i++) if (junk[i] >= 0) close(junk[i]);
    munmap((void *)live, 4096);
}

int main(void) {
    printf("torture_fork: fork() address-space semantics\n\n");
    sc1_shared_anon();
    sc2_private_cow();
    sc3_shared_counter();
    sc4_inherited_fds();
    sc5_worker_shape();
    printf("\nResult: %s (%d failure%s)\n",
           failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures;
}
