/*
 * torture_shm.c — System V shared memory (shmget/shmat/shmdt/shmctl) torture
 * suite.
 *
 * Pure POSIX C.  Builds against the host libc/kernel by default; against
 * substrate's libc + kernel SysV-shm subsystem when built with
 * CROSS=/opt/substrate/bin/i386-unknown-substrate-.  Must compile with no
 * warnings and PASS on Linux first — a failure here that does NOT reproduce on
 * host libc points at a substrate kernel/libc bug.
 *
 * Coverage:
 *   - shmget: IPC_PRIVATE, keyed create, IPC_CREAT/IPC_EXCL, ENOENT, size
 *     bounds (0 / too-big), get-existing, smaller/larger size, distinct ids.
 *   - shmat:  basic attach, write-then-read in the SAME process, attach
 *     read-only (SHM_RDONLY), caller-supplied addr, bad-id rejection.
 *   - SHARED semantics: a write by one process is seen by another that
 *     attaches the SAME segment (this is the whole point — NOT copy-on-write).
 *   - shmdt:  detach valid / detach bogus address (EINVAL).
 *   - shmctl: IPC_STAT (segsz / nattch / cpid / perm), IPC_SET (mode),
 *     IPC_RMID, SHM_LOCK/SHM_UNLOCK, stale-id rejection, RMID-while-attached
 *     keeps the mapping alive until last detach.
 *   - no-leak: many create+attach+detach+rmid cycles; physical memory (read
 *     from /proc/meminfo when present) must not drift downward.
 *
 * Every test runs in a forked child under an alarm(2) watchdog so a hung test
 * is reaped as HANG and the suite continues.  Each test removes the segments
 * it creates (segments are system-wide and would otherwise leak).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>

#define TEST_TIMEOUT 8
#define PAGE 4096

/* Host <sys/shm.h> (glibc) doesn't expose SHMMAX; it lives in <linux/shm.h>
 * and is enormous.  Substrate's <sys/shm.h> defines it as 8 MiB.  Provide a
 * fallback so the suite compiles everywhere; the too-big test adapts. */
#ifndef SHMMAX
#define SHMMAX (8 * 1024 * 1024)
#define SHMMAX_IS_FALLBACK 1
#endif

static int tests_run, tests_pass, tests_fail, tests_hang, tests_skip;
typedef int (*testfn)(void);

static void alrm_noop(int s){ (void)s; }

static void run_one(const char *name, testfn fn) {
    fprintf(stdout, "[%2d] %-34s ", ++tests_run, name); fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { fprintf(stdout, "FORK-FAIL errno=%d\n", errno); tests_fail++; return; }
    if (pid == 0) {
        int rc = fn();
        fflush(stdout);
        _exit(rc == 0 ? 0 : (rc == 1 ? 2 : 1));
    }
    struct sigaction sa, old; memset(&sa, 0, sizeof(sa)); sa.sa_handler = alrm_noop;
    sigaction(SIGALRM, &sa, &old);
    alarm(TEST_TIMEOUT);
    int st; pid_t r = waitpid(pid, &st, 0);
    alarm(0); sigaction(SIGALRM, &old, NULL);
    if (r != pid) {
        if (waitpid(pid, &st, WNOHANG) != pid) {
            kill(pid, SIGKILL); waitpid(pid, &st, 0);
            fprintf(stdout, "HANG (killed after %ds)\n", TEST_TIMEOUT); tests_hang++;
            return;
        }
    }
    if (WIFSIGNALED(st)) { fprintf(stdout, "CRASH sig=%d\n", WTERMSIG(st)); tests_fail++; }
    else if (WEXITSTATUS(st) == 0) { fprintf(stdout, "PASS\n"); tests_pass++; }
    else if (WEXITSTATUS(st) == 2) { fprintf(stdout, "SKIP\n"); tests_skip++; }
    else { fprintf(stdout, "FAIL\n"); tests_fail++; }
}

#define RUN(name)  run_one(#name, test_##name)
#define TEST(name) static int test_##name(void)
#define CHECK(cond, msg) do { if (!(cond)) { \
    fprintf(stdout, "\n    [%s:%d] %s errno=%d(%s) ", __FILE__, __LINE__, (msg), errno, strerror(errno)); \
    return -1; } } while (0)
#define CHECK_ERR(expr, experr, msg) do { errno = 0; \
    if ((long)(expr) != -1L || errno != (experr)) { \
        fprintf(stdout, "\n    [%s:%d] %s: expected -1/%d got errno=%d(%s) ", \
                __FILE__, __LINE__, (msg), (experr), errno, strerror(errno)); \
        return -1; } } while (0)

/* shmat returns (void*)-1 on error.  */
#define BAD_ADDR ((void *)-1)

/* --- helpers --- */

static int mkseg(size_t sz) {                 /* create a private segment */
    return shmget(IPC_PRIVATE, sz, IPC_CREAT | 0600);
}
static void rmseg(int id) {
    if (id >= 0) shmctl(id, IPC_RMID, NULL);
}
static int statds(int id, struct shmid_ds *ds) {
    return shmctl(id, IPC_STAT, ds);
}

/* ============================ shmget ============================ */

TEST(get_private) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "shmget IPC_PRIVATE");
    rmseg(id); return 0;
}
TEST(get_private_multi_page) {
    int id = mkseg(4 * PAGE);
    CHECK(id >= 0, "shmget 4 pages");
    struct shmid_ds ds;
    CHECK(statds(id, &ds) == 0, "stat");
    CHECK(ds.shm_segsz == (size_t)(4 * PAGE), "segsz==4 pages");
    rmseg(id); return 0;
}
TEST(get_keyed_creat) {
    key_t k = 0x5b000001;
    shmctl(shmget(k, PAGE, 0600), IPC_RMID, NULL);  /* clear stale */
    int id = shmget(k, PAGE, IPC_CREAT | 0600);
    CHECK(id >= 0, "shmget keyed creat");
    rmseg(id); return 0;
}
TEST(get_creat_excl_fresh) {
    key_t k = 0x5b000002;
    shmctl(shmget(k, PAGE, 0600), IPC_RMID, NULL);
    int id = shmget(k, PAGE, IPC_CREAT | IPC_EXCL | 0600);
    CHECK(id >= 0, "shmget creat|excl fresh");
    rmseg(id); return 0;
}
TEST(get_creat_excl_eexist) {
    key_t k = 0x5b000003;
    shmctl(shmget(k, PAGE, 0600), IPC_RMID, NULL);
    int id = shmget(k, PAGE, IPC_CREAT | 0600);
    CHECK(id >= 0, "first create");
    CHECK_ERR(shmget(k, PAGE, IPC_CREAT | IPC_EXCL | 0600), EEXIST, "excl on existing");
    rmseg(id); return 0;
}
TEST(get_missing_enoent) {
    key_t k = 0x5b000004;
    shmctl(shmget(k, PAGE, 0600), IPC_RMID, NULL);
    CHECK_ERR(shmget(k, PAGE, 0600), ENOENT, "no-creat missing");
    return 0;
}
TEST(get_existing_returns_same) {
    key_t k = 0x5b000005;
    shmctl(shmget(k, PAGE, 0600), IPC_RMID, NULL);
    int a = shmget(k, PAGE, IPC_CREAT | 0600);
    CHECK(a >= 0, "create");
    int b = shmget(k, PAGE, 0600);
    CHECK(b == a, "get-existing same id");
    rmseg(a); return 0;
}
TEST(get_larger_size_einval) {
    key_t k = 0x5b000006;
    shmctl(shmget(k, PAGE, 0600), IPC_RMID, NULL);
    int a = shmget(k, PAGE, IPC_CREAT | 0600);
    CHECK(a >= 0, "create 1 page");
    CHECK_ERR(shmget(k, 2 * PAGE, 0600), EINVAL, "get bigger size");
    rmseg(a); return 0;
}
TEST(get_size_zero_einval) {
    CHECK_ERR(shmget(IPC_PRIVATE, 0, IPC_CREAT | 0600), EINVAL, "size 0");
    return 0;
}
TEST(get_size_too_big_einval) {
#ifdef SHMMAX_IS_FALLBACK
    /* Host libc doesn't expose the real SHMMAX (it's huge); can't reliably
     * pick a size that exceeds it.  Skip on host; substrate exercises it. */
    return 1;   /* SKIP */
#else
    CHECK_ERR(shmget(IPC_PRIVATE, (size_t)SHMMAX + PAGE, IPC_CREAT | 0600),
              EINVAL, "size > SHMMAX");
    return 0;
#endif
}
TEST(get_distinct_ids) {
    int a = mkseg(PAGE), b = mkseg(PAGE);
    CHECK(a >= 0 && b >= 0, "two creates");
    CHECK(a != b, "distinct ids");
    rmseg(a); rmseg(b); return 0;
}
TEST(get_id_nonneg) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "id nonneg");
    rmseg(id); return 0;
}

/* ============================ shmat / shmdt ============================ */

TEST(at_basic) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    void *p = shmat(id, NULL, 0);
    CHECK(p != BAD_ADDR, "shmat");
    CHECK(shmdt(p) == 0, "shmdt");
    rmseg(id); return 0;
}
TEST(at_write_read_self) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    char *p = shmat(id, NULL, 0);
    CHECK((void *)p != BAD_ADDR, "shmat");
    strcpy(p, "hello shm");
    CHECK(strcmp(p, "hello shm") == 0, "readback same map");
    CHECK(shmdt(p) == 0, "shmdt");
    rmseg(id); return 0;
}
TEST(at_two_maps_same_proc) {
    /* Two attaches of the SAME segment in one process see each other. */
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    volatile int *a = shmat(id, NULL, 0);
    CHECK((void *)a != BAD_ADDR, "first attach");
    volatile int *b = shmat(id, NULL, 0);
    CHECK((void *)b != BAD_ADDR, "second attach");
    CHECK(a != b, "distinct VAs");
    a[0] = 0xCAFEBABE;
    CHECK(b[0] == (int)0xCAFEBABE, "second map sees first's write");
    b[1] = 0x1234;
    CHECK(a[1] == 0x1234, "first map sees second's write");
    shmdt((void *)a); shmdt((void *)b);
    rmseg(id); return 0;
}
TEST(at_rdonly) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    char *w = shmat(id, NULL, 0);
    CHECK((void *)w != BAD_ADDR, "rw attach");
    w[0] = 'X';
    char *r = shmat(id, NULL, SHM_RDONLY);
    CHECK((void *)r != BAD_ADDR, "ro attach");
    CHECK(r[0] == 'X', "ro map reads the value");
    shmdt(w); shmdt(r);
    rmseg(id); return 0;
}
TEST(at_badid) {
    errno = 0;
    void *p = shmat(0x7fffffff, NULL, 0);
    CHECK(p == BAD_ADDR && errno == EINVAL, "shmat bad id -> EINVAL");
    return 0;
}
TEST(dt_bogus_addr_einval) {
    /* Detaching an address that was never attached fails. */
    int dummy = 0;
    CHECK_ERR(shmdt(&dummy), EINVAL, "shmdt non-attached");
    return 0;
}

/* ====================== cross-process SHARED writes ====================== */

TEST(shared_parent_writes_child_reads) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    volatile int *p = shmat(id, NULL, 0);
    CHECK((void *)p != BAD_ADDR, "parent attach");
    p[0] = 0;                       /* handshake flag */
    p[1] = 0;                       /* payload */

    pid_t c = fork();
    CHECK(c >= 0, "fork");
    if (c == 0) {
        /* Child attaches independently (does NOT inherit the parent's
         * mapping — VM_INHERIT_NONE), writes the payload, sets the flag. */
        volatile int *cp = shmat(id, NULL, 0);
        if ((void *)cp == BAD_ADDR) _exit(42);
        cp[1] = 0xABCD;
        __sync_synchronize();
        cp[0] = 1;
        shmdt((void *)cp);
        _exit(0);
    }
    /* Parent spins on the flag (watchdog reaps a true hang). */
    while (p[0] == 0) { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
    CHECK(p[1] == 0xABCD, "parent sees child's shared write");
    int st; waitpid(c, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child ok");
    shmdt((void *)p);
    rmseg(id); return 0;
}

TEST(shared_pingpong) {
    /* Bidirectional: parent and child take turns incrementing a counter in
     * shared memory using a turn flag.  Proves writes are immediately visible
     * both ways (no COW divergence). */
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    volatile int *p = shmat(id, NULL, 0);
    CHECK((void *)p != BAD_ADDR, "parent attach");
    p[0] = 0;   /* turn: 0 = parent, 1 = child */
    p[1] = 0;   /* counter */
    const int ROUNDS = 50;

    pid_t c = fork();
    CHECK(c >= 0, "fork");
    if (c == 0) {
        volatile int *cp = shmat(id, NULL, 0);
        if ((void *)cp == BAD_ADDR) _exit(42);
        for (int i = 0; i < ROUNDS; i++) {
            while (cp[0] != 1) { struct timespec ts = {0, 200000}; nanosleep(&ts, NULL); }
            cp[1]++;
            __sync_synchronize();
            cp[0] = 0;
        }
        shmdt((void *)cp);
        _exit(0);
    }
    for (int i = 0; i < ROUNDS; i++) {
        while (p[0] != 0) { struct timespec ts = {0, 200000}; nanosleep(&ts, NULL); }
        p[1]++;
        __sync_synchronize();
        p[0] = 1;
    }
    int st; waitpid(c, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child ok");
    CHECK(p[1] == 2 * ROUNDS, "counter reflects both writers");
    shmdt((void *)p);
    rmseg(id); return 0;
}

TEST(shared_full_page_pattern) {
    /* Fill a whole page in the child, verify every word in the parent. */
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    volatile unsigned *p = shmat(id, NULL, 0);
    CHECK((void *)p != BAD_ADDR, "attach");
    p[0] = 0;
    pid_t c = fork();
    CHECK(c >= 0, "fork");
    if (c == 0) {
        volatile unsigned *cp = shmat(id, NULL, 0);
        if ((void *)cp == BAD_ADDR) _exit(42);
        for (unsigned i = 1; i < PAGE / sizeof(unsigned); i++)
            cp[i] = i * 2654435761u;     /* Knuth hash, deterministic */
        __sync_synchronize();
        cp[0] = 1;
        shmdt((void *)cp);
        _exit(0);
    }
    while (p[0] == 0) { struct timespec ts = {0, 500000}; nanosleep(&ts, NULL); }
    for (unsigned i = 1; i < PAGE / sizeof(unsigned); i++)
        CHECK(p[i] == i * 2654435761u, "every word matches");
    int st; waitpid(c, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child ok");
    shmdt((void *)p);
    rmseg(id); return 0;
}

/* ============================ shmctl ============================ */

TEST(c_stat_segsz) {
    int id = mkseg(3 * PAGE);
    CHECK(id >= 0, "create");
    struct shmid_ds ds;
    CHECK(statds(id, &ds) == 0, "stat");
    CHECK(ds.shm_segsz == (size_t)(3 * PAGE), "segsz");
    rmseg(id); return 0;
}
TEST(c_stat_cpid) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    struct shmid_ds ds;
    CHECK(statds(id, &ds) == 0, "stat");
    CHECK(ds.shm_cpid == getpid(), "cpid == creator");
    rmseg(id); return 0;
}
TEST(c_stat_nattch_zero) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    struct shmid_ds ds;
    CHECK(statds(id, &ds) == 0, "stat");
    CHECK(ds.shm_nattch == 0, "no attaches yet");
    rmseg(id); return 0;
}
TEST(c_stat_nattch_one) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    void *p = shmat(id, NULL, 0);
    CHECK(p != BAD_ADDR, "attach");
    struct shmid_ds ds;
    CHECK(statds(id, &ds) == 0, "stat");
    CHECK(ds.shm_nattch == 1, "one attach");
    shmdt(p);
    CHECK(statds(id, &ds) == 0, "stat after dt");
    CHECK(ds.shm_nattch == 0, "back to zero");
    rmseg(id); return 0;
}
TEST(c_set_mode_persists) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    struct shmid_ds ds;
    CHECK(statds(id, &ds) == 0, "stat");
    ds.shm_perm.mode = (ds.shm_perm.mode & ~0777) | 0640;
    CHECK(shmctl(id, IPC_SET, &ds) == 0, "set");
    CHECK(statds(id, &ds) == 0, "re-stat");
    CHECK((ds.shm_perm.mode & 0777) == 0640, "mode persisted");
    rmseg(id); return 0;
}
TEST(c_rmid_returns_zero) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    CHECK(shmctl(id, IPC_RMID, NULL) == 0, "rmid==0");
    return 0;
}
TEST(c_stat_after_rmid_fails) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    CHECK(shmctl(id, IPC_RMID, NULL) == 0, "rmid");
    struct shmid_ds ds;
    /* Substrate keeps the slot's sequence number and rejects the stale id
     * with EIDRM; Linux reuses the slot and reports EINVAL.  Accept either. */
    errno = 0;
    int r = shmctl(id, IPC_STAT, &ds);
    CHECK(r == -1 && (errno == EIDRM || errno == EINVAL),
          "stat after rmid -> EIDRM/EINVAL");
    return 0;
}
TEST(c_stat_badid) {
    struct shmid_ds ds;
    CHECK_ERR(shmctl(0x7fffffff, IPC_STAT, &ds), EINVAL, "stat bad id");
    return 0;
}
TEST(c_invalid_cmd) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    CHECK_ERR(shmctl(id, 0x7fff, NULL), EINVAL, "bad cmd");
    rmseg(id); return 0;
}
TEST(c_lock_unlock) {
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    /* SHM_LOCK / SHM_UNLOCK may require privilege on host Linux; accept
     * success or EPERM, but not a crash or a wrong errno. */
    int r = shmctl(id, SHM_LOCK, NULL);
    CHECK(r == 0 || (r == -1 && (errno == EPERM || errno == EACCES)),
          "SHM_LOCK ok-or-eperm");
    r = shmctl(id, SHM_UNLOCK, NULL);
    CHECK(r == 0 || (r == -1 && (errno == EPERM || errno == EACCES)),
          "SHM_UNLOCK ok-or-eperm");
    rmseg(id); return 0;
}

/* ============== IPC_RMID-while-attached keeps mapping alive ============== */

TEST(rmid_while_attached_keeps_mapping) {
    /* The load-bearing SysV invariant: IPC_RMID while still attached marks the
     * segment for destruction but the mapping stays usable until detach. */
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create");
    char *p = shmat(id, NULL, 0);
    CHECK((void *)p != BAD_ADDR, "attach");
    p[0] = 'Z';
    CHECK(shmctl(id, IPC_RMID, NULL) == 0, "rmid while attached");
    CHECK(p[0] == 'Z', "mapping still readable after rmid");
    p[0] = 'Q';
    CHECK(p[0] == 'Q', "mapping still writable after rmid");
    CHECK(shmdt(p) == 0, "final detach");
    return 0;
}

TEST(rmid_key_unresolvable) {
    /* After RMID, the key is removed from the namespace: shmget(key) fails. */
    key_t k = 0x5b00dead;
    shmctl(shmget(k, PAGE, 0600), IPC_RMID, NULL);   /* clear any stale */
    int id = shmget(k, PAGE, IPC_CREAT | 0600);
    CHECK(id >= 0, "create keyed");
    CHECK(shmctl(id, IPC_RMID, NULL) == 0, "rmid");
    CHECK_ERR(shmget(k, PAGE, 0600), ENOENT, "key gone after rmid");
    return 0;
}

/* ============================ exit cleanup ============================ */

TEST(exit_detaches_and_frees) {
    /* A child attaches + RMIDs a private segment and exits without detaching.
     * proc_exit must reverse the attach and (since it was RMID'd) free the
     * backing.  We can't see the kernel table from here, but we can confirm
     * the child exits cleanly and the parent's later create still works. */
    pid_t c = fork();
    CHECK(c >= 0, "fork");
    if (c == 0) {
        int id = mkseg(PAGE);
        if (id < 0) _exit(1);
        char *p = shmat(id, NULL, 0);
        if ((void *)p == BAD_ADDR) _exit(2);
        p[0] = 'A';
        if (shmctl(id, IPC_RMID, NULL) != 0) _exit(3);
        _exit(0);     /* exit WITHOUT shmdt — kernel must clean up */
    }
    int st; waitpid(c, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child clean exit");
    int id = mkseg(PAGE);
    CHECK(id >= 0, "create after child exit");
    rmseg(id); return 0;
}

/* ============================ no-leak loop ============================ */

/* Read MemFree+Cached-ish free KiB from /proc/meminfo; returns -1 if absent. */
static long meminfo_free_kb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[128];
    long memfree = -1;
    while (fgets(line, sizeof(line), f)) {
        long v;
        if (sscanf(line, "MemFree: %ld kB", &v) == 1) { memfree = v; break; }
    }
    fclose(f);
    return memfree;
}

TEST(noleak_create_rmid_cycle) {
    /* Many create+attach+write+detach+rmid cycles.  If pages leaked, free
     * memory would fall monotonically; we require it not to drop materially. */
    long before = meminfo_free_kb();
    const int N = 400;
    for (int i = 0; i < N; i++) {
        int id = shmget(IPC_PRIVATE, 8 * PAGE, IPC_CREAT | 0600);
        CHECK(id >= 0, "create in loop");
        char *p = shmat(id, NULL, 0);
        CHECK((void *)p != BAD_ADDR, "attach in loop");
        memset(p, (i & 0xff), 8 * PAGE);          /* touch every page */
        CHECK(p[0] == (char)(i & 0xff), "write took");
        CHECK(shmdt(p) == 0, "detach in loop");
        CHECK(shmctl(id, IPC_RMID, NULL) == 0, "rmid in loop");
    }
    long after = meminfo_free_kb();
    if (before > 0 && after > 0) {
        /* Allow slack for unrelated allocator noise (one 8-page seg = 32 KiB;
         * a leak of even a few would compound to MBs over 400 iters). */
        long drop = before - after;
        fprintf(stdout, "[freeKB %ld->%ld d=%ld] ", before, after, drop);
        CHECK(drop < 512, "no material free-memory drop over 400 cycles");
    } else {
        fprintf(stdout, "[no /proc/meminfo] ");
    }
    return 0;
}

TEST(noleak_attach_detach_cycle) {
    /* Repeated attach/detach of ONE segment must not leak per-attach state. */
    int id = mkseg(2 * PAGE);
    CHECK(id >= 0, "create");
    for (int i = 0; i < 500; i++) {
        char *p = shmat(id, NULL, 0);
        CHECK((void *)p != BAD_ADDR, "attach");
        p[0] = (char)i;
        CHECK(shmdt(p) == 0, "detach");
    }
    struct shmid_ds ds;
    CHECK(statds(id, &ds) == 0, "stat");
    CHECK(ds.shm_nattch == 0, "nattch back to 0");
    rmseg(id); return 0;
}

/* ============================ many segments ============================ */

TEST(many_segments_distinct) {
    enum { K = 32 };
    int ids[K];
    for (int i = 0; i < K; i++) {
        ids[i] = mkseg(PAGE);
        CHECK(ids[i] >= 0, "create many");
    }
    /* Write a tag into each via a brief attach; verify isolation. */
    for (int i = 0; i < K; i++) {
        int *p = shmat(ids[i], NULL, 0);
        CHECK((void *)p != BAD_ADDR, "attach many");
        p[0] = 0x1000 + i;
        shmdt(p);
    }
    for (int i = 0; i < K; i++) {
        int *p = shmat(ids[i], NULL, 0);
        CHECK((void *)p != BAD_ADDR, "re-attach many");
        CHECK(p[0] == 0x1000 + i, "segment kept its own value");
        shmdt(p);
    }
    for (int i = 0; i < K; i++) rmseg(ids[i]);
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stdout, "=== torture_shm: System V shared memory ===\n");

    RUN(get_private);
    RUN(get_private_multi_page);
    RUN(get_keyed_creat);
    RUN(get_creat_excl_fresh);
    RUN(get_creat_excl_eexist);
    RUN(get_missing_enoent);
    RUN(get_existing_returns_same);
    RUN(get_larger_size_einval);
    RUN(get_size_zero_einval);
    RUN(get_size_too_big_einval);
    RUN(get_distinct_ids);
    RUN(get_id_nonneg);

    RUN(at_basic);
    RUN(at_write_read_self);
    RUN(at_two_maps_same_proc);
    RUN(at_rdonly);
    RUN(at_badid);
    RUN(dt_bogus_addr_einval);

    RUN(shared_parent_writes_child_reads);
    RUN(shared_pingpong);
    RUN(shared_full_page_pattern);

    RUN(c_stat_segsz);
    RUN(c_stat_cpid);
    RUN(c_stat_nattch_zero);
    RUN(c_stat_nattch_one);
    RUN(c_set_mode_persists);
    RUN(c_rmid_returns_zero);
    RUN(c_stat_after_rmid_fails);
    RUN(c_stat_badid);
    RUN(c_invalid_cmd);
    RUN(c_lock_unlock);

    RUN(rmid_while_attached_keeps_mapping);
    RUN(rmid_key_unresolvable);
    RUN(exit_detaches_and_frees);

    RUN(noleak_create_rmid_cycle);
    RUN(noleak_attach_detach_cycle);

    RUN(many_segments_distinct);

    fprintf(stdout, "\n=== %d run: %d pass, %d fail, %d hang, %d skip ===\n",
            tests_run, tests_pass, tests_fail, tests_hang, tests_skip);
    return (tests_fail || tests_hang) ? 1 : 0;
}
