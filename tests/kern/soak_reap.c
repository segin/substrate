/*
 * soak_reap.c — cumulative process/thread-reap leak soak test.
 *
 * Two modes:
 *
 *  SEQUENTIAL (default): fork + (optionally exec) one victim at a time,
 *  SIGKILL a large fraction of them *while blocked in a syscall*, reap.
 *
 *  BATCH (soak_reap batch <nprocs> <threads_per> <nbatches>): the
 *  high-intensity stressor.  Each batch forks <nprocs> victim processes
 *  *concurrently*, each of which spawns <threads_per> secondary threads
 *  that park in the kernel (futex via pthread_cond_wait) so the whole
 *  batch is ~nprocs*(1+threads_per) live threads, then SIGKILLs the
 *  entire batch at once and waitpid-reaps every one.  This slams the
 *  abnormal-exit / proc+thread-reap teardown path with maximal
 *  concurrency — e.g. 400 processes / ~1000 threads.
 *
 * Every batch (or SNAP_EVERY iters) it reads /proc/meminfo MemFree so a
 * real physical leak shows; the kernel's debug=vm_leak + census lines
 * (per proc_exit) show pmap/pager/vnode/thread/proc balance.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>
#include <sys/futex.h>
#include <arch/i386/syscall.h>
#ifdef SOAK_THREADS
#include <pthread.h>
#endif

extern long syscall(long number, ...);
extern char **environ;

#define SNAP_EVERY 20
#define SELF_PATH  "/soak"
#define NCOPIES    128
#define MAX_BATCH  1024

enum { B_READ = 0, B_PAUSE, B_NANOSLEEP, B_FUTEX, B_NMODES };

/* ---------- counter snapshotting ---------- */
static char mibuf[8192];
static int slurp(const char *path, char *buf, int cap) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int off = 0, n;
    while (off < cap - 1 && (n = read(fd, buf + off, cap - 1 - off)) > 0) off += n;
    close(fd);
    buf[off] = '\0';
    return off;
}
static long memfree_kb(void) {
    if (slurp("/proc/meminfo", mibuf, sizeof mibuf) <= 0) return -1;
    const char *p = strstr(mibuf, "MemFree:");
    if (!p) return -1;
    p += 8; while (*p == ' ') p++;
    return strtol(p, NULL, 10);
}

/* struct pmap_stats: total_pmaps at index 10, active_pmaps at 11. */
static void pmap_counts(unsigned *total, unsigned *active) {
    uint32_t st[16];
    memset(st, 0, sizeof st);
    syscall(SYS_PMAP_STATS, st);
    if (total)  *total  = st[10];
    if (active) *active = st[11];
}

/* ---------- victim ---------- */
#ifdef SOAK_THREADS
static pthread_mutex_t tmtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  tcond = PTHREAD_COND_INITIALIZER;
static void *blocker_thread(void *arg) {
    (void)arg;
    pthread_mutex_lock(&tmtx);
    for (;;) pthread_cond_wait(&tcond, &tmtx);
    return NULL;
}
static void spawn_threads(int n) {
    for (int k = 0; k < n; k++) {
        pthread_t th;
        if (pthread_create(&th, NULL, blocker_thread, NULL) == 0)
            pthread_detach(th);
    }
}
#else
static void spawn_threads(int n) { (void)n; }
#endif

static void victim_block(int mode) {
    switch (mode) {
    case B_READ: { int p[2]; if (pipe(p)==0){char c;(void)read(p[0],&c,1);} else pause(); break; }
    case B_PAUSE: pause(); break;
    case B_NANOSLEEP: { struct timespec ts={1000,0}; while(nanosleep(&ts,NULL)==0||errno==EINTR){ts.tv_sec=1000;} break; }
    case B_FUTEX: { static volatile int w=0; for(;;) syscall(SYS_FUTEX,(long)&w,(long)FUTEX_WAIT,0L,0L); break; }
    default: break;
    }
    _exit(7);
}

/* A victim process: spawn threads, then block forever. */
static int victim_main(int mode, int nthreads, int wfd) {
    spawn_threads(nthreads);
    if (wfd >= 0) { char r='R'; (void)write(wfd,&r,1); close(wfd); }
    victim_block(mode);
    return 7;
}

/* ---------- BATCH stressor ---------- */
static pid_t pids[MAX_BATCH];

static int run_batch(int nprocs, int threads_per, int nbatches) {
    if (nprocs > MAX_BATCH) nprocs = MAX_BATCH;
    unsigned pt = 0, pa = 0;
    printf("soak_reap BATCH: nprocs=%d threads_per=%d nbatches=%d "
           "(~%d threads/batch)\n",
           nprocs, threads_per, nbatches, nprocs * (1 + threads_per));
    pmap_counts(&pt, &pa);
    printf("SNAP batch=0 MemFree_kB=%ld pmaps_total=%u pmaps_active=%u\n",
           memfree_kb(), pt, pa);

    for (int b = 1; b <= nbatches; b++) {
        int spawned = 0;
        for (int i = 0; i < nprocs; i++) {
            pid_t pid = fork();
            if (pid < 0) {
                /* Out of resources mid-batch: record how far we got. */
                printf("soak_reap: fork FAILED batch=%d after %d procs errno=%d\n",
                       b, spawned, errno);
                break;
            }
            if (pid == 0) {
                int mode = i % B_NMODES;
                /* In-process: spawn threads then block.  No sync pipe (would
                 * need nprocs fds) — parent paces with a sleep instead. */
                victim_main(mode, threads_per, -1);
                _exit(9);
            }
            pids[spawned++] = pid;
        }

        /* Let the batch spawn its threads and reach the blocking syscalls. */
        struct timespec ts = { 0, 60 * 1000 * 1000 }; /* 60 ms */
        nanosleep(&ts, NULL);

        /* SIGKILL the whole batch while every thread is blocked. */
        for (int i = 0; i < spawned; i++) kill(pids[i], SIGKILL);

        /* Drain ALL children via waitpid(-1) until ECHILD.  This is robust
         * against reparenting / pid races: it counts every reapable child.
         * If fewer than `spawned` are reaped, children vanished; if it never
         * reaches ECHILD, a child is stuck un-reapable (a teardown wedge). */
        int reaped = 0;
        for (;;) {
            int st = 0;
            pid_t w = waitpid(-1, &st, 0);
            if (w <= 0) break;            /* ECHILD (all reaped) or error */
            reaped++;
        }

        /* Quiescent point: every child reaped, only the parent remains.
         * pmaps_total should be back to its batch-0 baseline; a climbing
         * baseline is a per-batch pmap leak. */
        pmap_counts(&pt, &pa);
        printf("SNAP batch=%d spawned=%d reaped=%d MemFree_kB=%ld "
               "pmaps_total=%u pmaps_active=%u\n",
               b, spawned, reaped, memfree_kb(), pt, pa);
        fflush(stdout);
    }

    printf("soak_reap: BATCH finished\n");
    printf("Result: soak_reap COMPLETE\n");
    return 0;
}

/* ---------- waitpid(specific-pid) concurrency probe ----------
 * Fork <n> concurrent blockers, SIGKILL all, then waitpid(EACH SPECIFIC pid).
 * Count how many the specific-pid waits reap vs. how many are left as
 * un-reaped zombies that a following waitpid(-1) drain then mops up.  A
 * nonzero drain count means waitpid(pid) spuriously failed for a live
 * zombie — the shell-style reap leak. */
static int run_wtest(int n, int rounds) {
    if (n > MAX_BATCH) n = MAX_BATCH;
    printf("soak_reap WTEST: n=%d rounds=%d\n", n, rounds);
    for (int r = 1; r <= rounds; r++) {
        int spawned = 0;
        for (int i = 0; i < n; i++) {
            pid_t pid = fork();
            if (pid < 0) break;
            if (pid == 0) { victim_main(i % B_NMODES, 0, -1); _exit(9); }
            pids[spawned++] = pid;
        }
        struct timespec ts = { 0, 60 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        for (int i = 0; i < spawned; i++) kill(pids[i], SIGKILL);

        /* Phase 1: waitpid EACH SPECIFIC pid once. */
        int spec_ok = 0, spec_echild = 0, spec_other = 0;
        for (int i = 0; i < spawned; i++) {
            int st = 0;
            errno = 0;
            pid_t w = waitpid(pids[i], &st, 0);
            if (w == pids[i]) spec_ok++;
            else if (errno == ECHILD) spec_echild++;
            else spec_other++;
        }
        /* Phase 2: drain any zombies waitpid(pid) failed to reap. */
        int drained = 0;
        for (;;) {
            int st = 0;
            pid_t w = waitpid(-1, &st, 0);
            if (w <= 0) break;
            drained++;
        }
        unsigned pt = 0, pa = 0;
        pmap_counts(&pt, &pa);
        printf("WTEST round=%d spawned=%d spec_ok=%d spec_ECHILD=%d "
               "spec_other=%d drained_leftover=%d MemFree_kB=%ld pmaps=%u\n",
               r, spawned, spec_ok, spec_echild, spec_other, drained,
               memfree_kb(), pt);
        fflush(stdout);
    }
    printf("Result: soak_reap COMPLETE\n");
    return 0;
}

/* ---------- SEQUENTIAL mode (original) ---------- */
static void snapshot_seq(int iter) {
    printf("SNAP iter=%d MemFree_kB=%ld\n", iter, memfree_kb());
    fflush(stdout);
}

static int run_sequential(int iters) {
    printf("soak_reap: sequential iters=%d\n", iters);
    snapshot_seq(0);
    for (int i = 1; i <= iters; i++) {
        int mode = (i - 1) % B_NMODES;
        int clean = (i % 5 == 0);
        int sp[2] = { -1, -1 };
        if (!clean && pipe(sp) != 0) { perror("pipe"); break; }
        pid_t pid = fork();
        if (pid < 0) { printf("soak_reap: fork FAILED iter %d errno=%d\n", i, errno); break; }
        if (pid == 0) {
            if (!clean && sp[0] >= 0) close(sp[0]);
            victim_main(mode, 0, clean ? -1 : sp[1]);
            _exit(9);
        }
        if (!clean) {
            close(sp[1]);
            char r; ssize_t got = read(sp[0], &r, 1); close(sp[0]);
            struct timespec ts = { 0, 3 * 1000 * 1000 };
            nanosleep(&ts, NULL);
            (void)got;
            kill(pid, SIGKILL);
        }
        int st = 0; waitpid(pid, &st, 0);
        if (i % SNAP_EVERY == 0) snapshot_seq(i);
    }
    snapshot_seq(iters);
    printf("Result: soak_reap COMPLETE\n");
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc >= 3 && strcmp(argv[1], "victim") == 0) {
        int mode = atoi(argv[2]);
        int nthreads = (argc >= 4) ? atoi(argv[3]) : 0;
        int wfd = (argc >= 5) ? atoi(argv[4]) : -1;
        return victim_main(mode, nthreads, wfd);
    }

    /* Batch spec is a single, space-free token so it survives the kernel's
     * space-delimited cmdline: "batch:<nprocs>:<threads_per>:<nbatches>". */
    if (argc >= 2 && strncmp(argv[1], "batch", 5) == 0) {
        int nprocs = 400, threads_per = 2, nbatches = 20;
        const char *p = argv[1] + 5;
        if (*p == ':') {
            nprocs = atoi(++p);
            const char *q = strchr(p, ':');
            if (q) { threads_per = atoi(++q); const char *r = strchr(q, ':');
                     if (r) nbatches = atoi(++r); }
        } else if (argc >= 3) {
            nprocs = atoi(argv[2]);
            if (argc >= 4) threads_per = atoi(argv[3]);
            if (argc >= 5) nbatches = atoi(argv[4]);
        }
        if (nprocs <= 0) nprocs = 400;
        if (threads_per < 0) threads_per = 2;
        if (nbatches <= 0) nbatches = 20;
        return run_batch(nprocs, threads_per, nbatches);
    }

    /* "wtest:<n>:<rounds>" — waitpid(specific-pid) concurrency probe. */
    if (argc >= 2 && strncmp(argv[1], "wtest", 5) == 0) {
        int n = 200, rounds = 10;
        const char *p = argv[1] + 5;
        if (*p == ':') { n = atoi(++p); const char *q = strchr(p, ':'); if (q) rounds = atoi(++q); }
        if (n <= 0) n = 200;
        if (rounds <= 0) rounds = 10;
        return run_wtest(n, rounds);
    }

    int iters = (argc > 1) ? atoi(argv[1]) : 300;
    if (iters <= 0) iters = 300;
    return run_sequential(iters);
}
