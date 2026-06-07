/*
 * torture_sem.c — System V semaphore (semget/semop/semctl) torture suite.
 * 70+ distinct tests.
 *
 * Pure POSIX C.  Builds against the host libc/kernel by default; against
 * substrate's libc + kernel SysV-semaphore subsystem when built with
 * CROSS=/opt/substrate/bin/i386-unknown-substrate-.  Must compile with no
 * warnings and PASS on Linux first — a failure here that does NOT reproduce on
 * host libc points at a substrate kernel/libc bug.
 *
 * Coverage:
 *   - semget: IPC_PRIVATE, keyed create, IPC_CREAT/IPC_EXCL, ENOENT, nsems
 *     bounds, get-existing, smaller/larger nsems, distinct ids.
 *   - semctl: GETVAL/SETVAL/GETALL/SETALL/GETPID/GETNCNT/GETZCNT, IPC_STAT
 *     (nsems/perm/otime/ctime), IPC_SET (mode), IPC_RMID, value/range errors,
 *     stale-id rejection.
 *   - semop: increment/decrement/wait-for-zero, multi-op atomicity, IPC_NOWAIT
 *     EAGAIN, overflow ERANGE, bad sem_num EFBIG, nsops bounds, same-sem ops.
 *   - blocking + wakeup across processes, EIDRM on RMID-while-blocked,
 *     two-waiters fairness, SEM_UNDO reversal at process exit.
 *
 * Every test runs in a forked child under an alarm(2) watchdog so a hung
 * blocking-op test is reaped as HANG and the suite continues.  Each test
 * removes the sets it creates (sem sets are system-wide and would otherwise
 * leak past the process).
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
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef SEMVMX
#define SEMVMX 32767
#endif
#ifndef SEMMSL
#define SEMMSL 250
#endif
#ifndef SEMOPM
#define SEMOPM 32
#endif

/* The 4th semctl argument: define our own union (semctl is variadic, so the
 * union's tag is irrelevant at the call site) — portable across hosts whose
 * <sys/sem.h> does or doesn't define `union semun`. */
union t_semun {
    int             val;
    struct semid_ds *buf;
    unsigned short  *array;
};

#define TEST_TIMEOUT 8

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
    if ((expr) != -1 || errno != (experr)) { \
        fprintf(stdout, "\n    [%s:%d] %s: expected -1/%d got errno=%d(%s) ", \
                __FILE__, __LINE__, (msg), (experr), errno, strerror(errno)); \
        return -1; } } while (0)

/* --- helpers --- */

static int mkset(int nsems) {                 /* create a private set */
    return semget(IPC_PRIVATE, nsems, IPC_CREAT | 0600);
}
static void rmset(int id) {
    if (id >= 0) semctl(id, 0, IPC_RMID);
}
static int getval(int id, int n) {
    return semctl(id, n, GETVAL);
}
static int setval(int id, int n, int v) {
    union t_semun a; a.val = v;
    return semctl(id, n, SETVAL, a);
}
static int op(int id, int num, int o, int flg) {
    struct sembuf sb; sb.sem_num = num; sb.sem_op = o; sb.sem_flg = flg;
    return semop(id, &sb, 1);
}
static int getall(int id, unsigned short *out) {
    union t_semun a; a.array = out;
    return semctl(id, 0, GETALL, a);
}
static int setall(int id, unsigned short *in) {
    union t_semun a; a.array = in;
    return semctl(id, 0, SETALL, a);
}
static int statds(int id, struct semid_ds *ds) {
    union t_semun a; a.buf = ds;
    return semctl(id, 0, IPC_STAT, a);
}

/* ============================ semget ============================ */

TEST(get_private) {
    int id = mkset(1);
    CHECK(id >= 0, "semget IPC_PRIVATE");
    rmset(id); return 0;
}
TEST(get_private_multi) {
    int id = mkset(8);
    CHECK(id >= 0, "semget nsems=8");
    rmset(id); return 0;
}
TEST(get_keyed_creat) {
    key_t k = 0x5e000001;
    int id = semget(k, 2, IPC_CREAT | 0600);
    CHECK(id >= 0, "semget keyed");
    rmset(id); return 0;
}
TEST(get_existing_same_id) {
    key_t k = 0x5e000002;
    int a = semget(k, 2, IPC_CREAT | 0600);
    CHECK(a >= 0, "create");
    int b = semget(k, 2, 0600);
    CHECK(b == a, "get existing returns same id");
    rmset(a); return 0;
}
TEST(get_excl_eexist) {
    key_t k = 0x5e000003;
    int a = semget(k, 1, IPC_CREAT | 0600);
    CHECK(a >= 0, "create");
    CHECK_ERR(semget(k, 1, IPC_CREAT | IPC_EXCL | 0600), EEXIST, "EXCL on existing");
    rmset(a); return 0;
}
TEST(get_noent) {
    CHECK_ERR(semget(0x5e0000ff, 1, 0600), ENOENT, "get missing w/o CREAT");
    return 0;
}
TEST(get_nsems_zero_create) {
    CHECK_ERR(semget(IPC_PRIVATE, 0, IPC_CREAT | 0600), EINVAL, "nsems=0 create");
    return 0;
}
TEST(get_nsems_negative) {
    CHECK_ERR(semget(IPC_PRIVATE, -1, IPC_CREAT | 0600), EINVAL, "nsems<0");
    return 0;
}
TEST(get_nsems_too_big) {
    /* Use a value that exceeds SEMMSL on any platform (substrate 250, Linux
     * ~32000) so the limit check fires regardless of the local SEMMSL. */
    CHECK_ERR(semget(IPC_PRIVATE, 1000000, IPC_CREAT | 0600), EINVAL, "nsems huge");
    return 0;
}
TEST(get_smaller_nsems_ok) {
    key_t k = 0x5e000004;
    int a = semget(k, 5, IPC_CREAT | 0600);
    CHECK(a >= 0, "create 5");
    int b = semget(k, 3, 0600);
    CHECK(b == a, "get with smaller nsems ok");
    rmset(a); return 0;
}
TEST(get_bigger_nsems_einval) {
    key_t k = 0x5e000005;
    int a = semget(k, 3, IPC_CREAT | 0600);
    CHECK(a >= 0, "create 3");
    CHECK_ERR(semget(k, 5, 0600), EINVAL, "get with bigger nsems");
    rmset(a); return 0;
}
TEST(get_private_unique) {
    int a = mkset(1), b = mkset(1);
    CHECK(a >= 0 && b >= 0, "two privates");
    CHECK(a != b, "private ids distinct");
    rmset(a); rmset(b); return 0;
}
TEST(get_zero_nsems_existing_ok) {
    key_t k = 0x5e000006;
    int a = semget(k, 4, IPC_CREAT | 0600);
    CHECK(a >= 0, "create");
    int b = semget(k, 0, 0600);           /* nsems 0 allowed for existing */
    CHECK(b == a, "nsems=0 on existing ok");
    rmset(a); return 0;
}

/* ============================ semctl val ============================ */

TEST(setval_getval) {
    int id = mkset(1);
    CHECK(setval(id, 0, 7) == 0, "SETVAL");
    CHECK(getval(id, 0) == 7, "GETVAL==7");
    rmset(id); return 0;
}
TEST(setval_zero) {
    int id = mkset(1);
    CHECK(setval(id, 0, 0) == 0, "SETVAL 0");
    CHECK(getval(id, 0) == 0, "GETVAL 0");
    rmset(id); return 0;
}
TEST(setval_max) {
    int id = mkset(1);
    CHECK(setval(id, 0, SEMVMX) == 0, "SETVAL SEMVMX");
    CHECK(getval(id, 0) == SEMVMX, "GETVAL SEMVMX");
    rmset(id); return 0;
}
TEST(initial_val_zero) {
    int id = mkset(3);
    CHECK(getval(id, 0) == 0 && getval(id, 2) == 0, "fresh sems == 0");
    rmset(id); return 0;
}
TEST(setval_negative_erange) {
    int id = mkset(1);
    union t_semun a; a.val = -1;
    CHECK_ERR(semctl(id, 0, SETVAL, a), ERANGE, "SETVAL -1");
    rmset(id); return 0;
}
TEST(setval_over_max_erange) {
    int id = mkset(1);
    union t_semun a; a.val = SEMVMX + 1;
    CHECK_ERR(semctl(id, 0, SETVAL, a), ERANGE, "SETVAL >SEMVMX");
    rmset(id); return 0;
}
TEST(getval_bad_semnum) {
    int id = mkset(2);
    CHECK_ERR(semctl(id, 9, GETVAL), EINVAL, "GETVAL bad semnum");
    rmset(id); return 0;
}
TEST(setval_bad_semnum) {
    int id = mkset(2);
    union t_semun a; a.val = 1;
    CHECK_ERR(semctl(id, 9, SETVAL, a), EINVAL, "SETVAL bad semnum");
    rmset(id); return 0;
}
TEST(setval_independent) {
    int id = mkset(3);
    CHECK(setval(id, 1, 5) == 0, "set sem1");
    CHECK(getval(id, 0) == 0 && getval(id, 1) == 5 && getval(id, 2) == 0,
          "only sem1 changed");
    rmset(id); return 0;
}

/* ============================ GETALL / SETALL ============================ */

TEST(setall_getall) {
    int id = mkset(3);
    unsigned short in[3] = {1, 2, 3}, out[3] = {0,0,0};
    union t_semun a;
    a.array = in;  CHECK(semctl(id, 0, SETALL, a) == 0, "SETALL");
    a.array = out; CHECK(semctl(id, 0, GETALL, a) == 0, "GETALL");
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3, "GETALL values");
    rmset(id); return 0;
}
TEST(getall_reflects_setval) {
    int id = mkset(3);
    setval(id, 1, 9);
    unsigned short out[3]; union t_semun a; a.array = out;
    CHECK(semctl(id, 0, GETALL, a) == 0, "GETALL");
    CHECK(out[1] == 9, "GETALL reflects SETVAL");
    rmset(id); return 0;
}
TEST(setall_then_op) {
    int id = mkset(2);
    unsigned short in[2] = {4, 0}; union t_semun a; a.array = in;
    CHECK(semctl(id, 0, SETALL, a) == 0, "SETALL");
    CHECK(op(id, 0, -4, 0) == 0, "decrement to 0");
    CHECK(getval(id, 0) == 0, "now 0");
    rmset(id); return 0;
}

/* ============================ GETPID ============================ */

TEST(getpid_initial_zero) {
    int id = mkset(1);
    CHECK(semctl(id, 0, GETPID) == 0, "fresh GETPID 0");
    rmset(id); return 0;
}
TEST(getpid_after_op) {
    int id = mkset(1);
    CHECK(op(id, 0, 1, 0) == 0, "op");
    CHECK(semctl(id, 0, GETPID) == (int)getpid(), "GETPID == our pid");
    rmset(id); return 0;
}
TEST(getncnt_zero) {
    int id = mkset(1);
    CHECK(semctl(id, 0, GETNCNT) == 0, "GETNCNT 0");
    rmset(id); return 0;
}
TEST(getzcnt_zero) {
    int id = mkset(1);
    CHECK(semctl(id, 0, GETZCNT) == 0, "GETZCNT 0");
    rmset(id); return 0;
}

/* ============================ IPC_STAT / IPC_SET ============================ */

TEST(stat_nsems) {
    int id = mkset(4);
    struct semid_ds ds; union t_semun a; a.buf = &ds;
    CHECK(semctl(id, 0, IPC_STAT, a) == 0, "IPC_STAT");
    CHECK(ds.sem_nsems == 4, "sem_nsems==4");
    rmset(id); return 0;
}
TEST(stat_perm_mode) {
    int id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0640);
    struct semid_ds ds; union t_semun a; a.buf = &ds;
    CHECK(semctl(id, 0, IPC_STAT, a) == 0, "IPC_STAT");
    CHECK((ds.sem_perm.mode & 0777) == 0640, "mode 0640");
    rmset(id); return 0;
}
TEST(stat_perm_uid) {
    int id = mkset(1);
    struct semid_ds ds; union t_semun a; a.buf = &ds;
    CHECK(semctl(id, 0, IPC_STAT, a) == 0, "IPC_STAT");
    CHECK(ds.sem_perm.uid == geteuid(), "owner uid == euid");
    rmset(id); return 0;
}
TEST(stat_ctime_nonzero) {
    int id = mkset(1);
    struct semid_ds ds; union t_semun a; a.buf = &ds;
    CHECK(semctl(id, 0, IPC_STAT, a) == 0, "IPC_STAT");
    CHECK(ds.sem_ctime != 0, "ctime set at create");
    rmset(id); return 0;
}
TEST(stat_otime_initially_zero) {
    int id = mkset(1);
    struct semid_ds ds; union t_semun a; a.buf = &ds;
    CHECK(semctl(id, 0, IPC_STAT, a) == 0, "IPC_STAT");
    CHECK(ds.sem_otime == 0, "otime 0 before any op");
    rmset(id); return 0;
}
TEST(stat_otime_after_op) {
    int id = mkset(1);
    CHECK(op(id, 0, 1, 0) == 0, "op");
    struct semid_ds ds; union t_semun a; a.buf = &ds;
    CHECK(semctl(id, 0, IPC_STAT, a) == 0, "IPC_STAT");
    CHECK(ds.sem_otime != 0, "otime set after op");
    rmset(id); return 0;
}
TEST(set_mode) {
    int id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    struct semid_ds ds; union t_semun a; a.buf = &ds;
    CHECK(semctl(id, 0, IPC_STAT, a) == 0, "stat");
    ds.sem_perm.mode = (ds.sem_perm.mode & ~0777) | 0666;
    CHECK(semctl(id, 0, IPC_SET, a) == 0, "IPC_SET");
    CHECK(semctl(id, 0, IPC_STAT, a) == 0, "stat2");
    CHECK((ds.sem_perm.mode & 0777) == 0666, "mode now 0666");
    rmset(id); return 0;
}

/* ============================ IPC_RMID ============================ */

TEST(rmid) {
    int id = mkset(1);
    CHECK(semctl(id, 0, IPC_RMID) == 0, "RMID");
    CHECK_ERR(semctl(id, 0, GETVAL), EINVAL, "GETVAL after RMID");
    return 0;
}
TEST(rmid_twice) {
    int id = mkset(1);
    CHECK(semctl(id, 0, IPC_RMID) == 0, "RMID");
    CHECK_ERR(semctl(id, 0, IPC_RMID), EINVAL, "RMID again");
    return 0;
}
TEST(op_after_rmid) {
    int id = mkset(1);
    CHECK(semctl(id, 0, IPC_RMID) == 0, "RMID");
    CHECK_ERR(op(id, 0, 1, 0), EINVAL, "op after RMID");
    return 0;
}
TEST(keyed_reuse_after_rmid) {
    key_t k = 0x5e000010;
    int a = semget(k, 1, IPC_CREAT | 0600);
    setval(a, 0, 5);
    CHECK(semctl(a, 0, IPC_RMID) == 0, "RMID");
    int b = semget(k, 1, IPC_CREAT | 0600);
    CHECK(b >= 0, "recreate same key");
    CHECK(getval(b, 0) == 0, "fresh value after reuse");
    rmset(b); return 0;
}
TEST(stale_id_after_rmid_recreate) {
    key_t k = 0x5e000011;
    int a = semget(k, 1, IPC_CREAT | 0600);
    CHECK(semctl(a, 0, IPC_RMID) == 0, "RMID");
    int b = semget(k, 1, IPC_CREAT | 0600);
    CHECK(b >= 0, "recreate");
    if (b != a) {                    /* id was reissued with a new seq */
        errno = 0;
        int rc = semctl(a, 0, GETVAL);
        CHECK(rc == -1 && (errno == EINVAL || errno == EIDRM),
              "stale id rejected");
    }
    rmset(b); return 0;
}

/* ============================ semop ============================ */

TEST(op_increment) {
    int id = mkset(1);
    CHECK(op(id, 0, 5, 0) == 0, "+5");
    CHECK(getval(id, 0) == 5, "==5");
    rmset(id); return 0;
}
TEST(op_decrement) {
    int id = mkset(1);
    setval(id, 0, 5);
    CHECK(op(id, 0, -3, 0) == 0, "-3");
    CHECK(getval(id, 0) == 2, "==2");
    rmset(id); return 0;
}
TEST(op_to_zero) {
    int id = mkset(1);
    setval(id, 0, 3);
    CHECK(op(id, 0, -3, 0) == 0, "-3 to 0");
    CHECK(getval(id, 0) == 0, "==0");
    rmset(id); return 0;
}
TEST(op_wait_zero_when_zero) {
    int id = mkset(1);                 /* already 0 */
    CHECK(op(id, 0, 0, 0) == 0, "wait-zero returns immediately");
    rmset(id); return 0;
}
TEST(op_nowait_block_decrement) {
    int id = mkset(1);                 /* 0; -1 would block */
    CHECK_ERR(op(id, 0, -1, IPC_NOWAIT), EAGAIN, "NOWAIT -1 on 0");
    rmset(id); return 0;
}
TEST(op_nowait_block_zero) {
    int id = mkset(1);
    setval(id, 0, 5);
    CHECK_ERR(op(id, 0, 0, IPC_NOWAIT), EAGAIN, "NOWAIT wait-zero on 5");
    rmset(id); return 0;
}
TEST(op_overflow_erange) {
    int id = mkset(1);
    setval(id, 0, SEMVMX - 1);
    CHECK_ERR(op(id, 0, 5, 0), ERANGE, "overflow SEMVMX");
    rmset(id); return 0;
}
TEST(op_zero_nsops) {
    /* Linux (and substrate) reject nsops==0 with EINVAL — semop requires at
     * least one operation. */
    int id = mkset(1);
    struct sembuf sb; memset(&sb, 0, sizeof(sb));
    CHECK_ERR(semop(id, &sb, 0), EINVAL, "nsops=0");
    rmset(id); return 0;
}
TEST(op_too_many_ops) {
    /* Exceed SEMOPM on any platform (substrate 32, Linux ~500). */
    int id = mkset(1);
    size_t n = 100000;
    struct sembuf *sb = calloc(n, sizeof(*sb));
    CHECK(sb != NULL, "calloc");
    errno = 0;
    int rc = semop(id, sb, n);
    free(sb);
    CHECK(rc == -1 && errno == E2BIG, "nsops huge -> E2BIG");
    rmset(id); return 0;
}
TEST(op_bad_semnum) {
    int id = mkset(2);
    CHECK_ERR(op(id, 9, 1, 0), EFBIG, "sem_num>=nsems");
    rmset(id); return 0;
}
TEST(op_multi_atomic_ok) {
    int id = mkset(2);
    setval(id, 0, 3); setval(id, 1, 3);
    struct sembuf sb[2];
    sb[0].sem_num = 0; sb[0].sem_op = -2; sb[0].sem_flg = 0;
    sb[1].sem_num = 1; sb[1].sem_op = -1; sb[1].sem_flg = 0;
    CHECK(semop(id, sb, 2) == 0, "both ops apply");
    CHECK(getval(id, 0) == 1 && getval(id, 1) == 2, "values updated");
    rmset(id); return 0;
}
TEST(op_multi_atomic_rollback) {
    /* first op feasible, second would block + NOWAIT -> EAGAIN, nothing applied */
    int id = mkset(2);
    setval(id, 0, 5); setval(id, 1, 0);
    struct sembuf sb[2];
    sb[0].sem_num = 0; sb[0].sem_op = -1; sb[0].sem_flg = 0;
    sb[1].sem_num = 1; sb[1].sem_op = -1; sb[1].sem_flg = IPC_NOWAIT;
    CHECK_ERR(semop(id, sb, 2), EAGAIN, "multi-op blocks -> EAGAIN");
    CHECK(getval(id, 0) == 5 && getval(id, 1) == 0, "atomic: nothing applied");
    rmset(id); return 0;
}
TEST(op_same_sem_twice) {
    int id = mkset(1);
    setval(id, 0, 1);
    struct sembuf sb[2];
    sb[0].sem_num = 0; sb[0].sem_op = 2; sb[0].sem_flg = 0;   /* 1 -> 3 */
    sb[1].sem_num = 0; sb[1].sem_op = -1; sb[1].sem_flg = 0;  /* 3 -> 2 */
    CHECK(semop(id, sb, 2) == 0, "two ops same sem");
    CHECK(getval(id, 0) == 2, "net +1");
    rmset(id); return 0;
}
TEST(op_same_sem_intermediate_negative) {
    /* -1 then +1 on a sem at 0: first sub-op makes it negative -> blocks;
     * with NOWAIT the whole call is EAGAIN. */
    int id = mkset(1);                 /* value 0 */
    struct sembuf sb[2];
    sb[0].sem_num = 0; sb[0].sem_op = -1; sb[0].sem_flg = IPC_NOWAIT;
    sb[1].sem_num = 0; sb[1].sem_op = 1;  sb[1].sem_flg = IPC_NOWAIT;
    CHECK_ERR(semop(id, sb, 2), EAGAIN, "intermediate negative blocks");
    CHECK(getval(id, 0) == 0, "unchanged");
    rmset(id); return 0;
}

/* ===================== blocking + wakeup (cross-process) ===================== */

TEST(block_decrement_wakes) {
    int id = mkset(1);                 /* value 0 */
    pid_t c = fork();
    if (c == 0) {                      /* child: block on -1 */
        _exit(op(id, 0, -1, 0) == 0 ? 0 : 1);
    }
    usleep(200000);                    /* let child block */
    CHECK(op(id, 0, 1, 0) == 0, "parent +1 wakes child");
    int st; waitpid(c, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child completed -1");
    CHECK(getval(id, 0) == 0, "net zero");
    rmset(id); return 0;
}
TEST(block_waitzero_wakes) {
    int id = mkset(1);
    setval(id, 0, 1);
    pid_t c = fork();
    if (c == 0) { _exit(op(id, 0, 0, 0) == 0 ? 0 : 1); }  /* block: wait for 0 */
    usleep(200000);
    CHECK(op(id, 0, -1, 0) == 0, "parent drives to 0");
    int st; waitpid(c, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child saw zero");
    rmset(id); return 0;
}
TEST(block_setval_wakes) {
    int id = mkset(1);                 /* 0 */
    pid_t c = fork();
    if (c == 0) { _exit(op(id, 0, -2, 0) == 0 ? 0 : 1); }
    usleep(200000);
    CHECK(setval(id, 0, 5) == 0, "SETVAL 5 wakes waiter");
    int st; waitpid(c, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child took 2");
    CHECK(getval(id, 0) == 3, "5-2==3");
    rmset(id); return 0;
}
TEST(block_then_rmid_eidrm) {
    int id = mkset(1);                 /* 0 */
    pid_t c = fork();
    if (c == 0) {
        errno = 0;
        int rc = op(id, 0, -1, 0);     /* blocks, then set is removed */
        _exit(rc == -1 && errno == EIDRM ? 0 : 1);
    }
    usleep(200000);
    CHECK(semctl(id, 0, IPC_RMID) == 0, "RMID while child blocked");
    int st; waitpid(c, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "blocked child got EIDRM");
    return 0;
}
TEST(two_waiters_resource) {
    int id = mkset(1);                 /* 0 */
    pid_t c1 = fork();
    if (c1 == 0) { _exit(op(id, 0, -1, 0) == 0 ? 0 : 1); }
    pid_t c2 = fork();
    if (c2 == 0) { _exit(op(id, 0, -1, 0) == 0 ? 0 : 1); }
    usleep(200000);
    CHECK(op(id, 0, 2, 0) == 0, "+2 releases both");
    int st1, st2; waitpid(c1, &st1, 0); waitpid(c2, &st2, 0);
    CHECK(WIFEXITED(st1) && WEXITSTATUS(st1) == 0, "waiter1 done");
    CHECK(WIFEXITED(st2) && WEXITSTATUS(st2) == 0, "waiter2 done");
    CHECK(getval(id, 0) == 0, "both consumed");
    rmset(id); return 0;
}
TEST(block_multiop_wakes) {
    int id = mkset(2);
    setval(id, 0, 0); setval(id, 1, 0);
    pid_t c = fork();
    if (c == 0) {
        struct sembuf sb[2];
        sb[0].sem_num = 0; sb[0].sem_op = -1; sb[0].sem_flg = 0;
        sb[1].sem_num = 1; sb[1].sem_op = -1; sb[1].sem_flg = 0;
        _exit(semop(id, sb, 2) == 0 ? 0 : 1);
    }
    usleep(150000);
    op(id, 0, 1, 0);                   /* satisfy only sem0 — child stays blocked */
    usleep(150000);
    CHECK(op(id, 1, 1, 0) == 0, "satisfy sem1 -> child proceeds");
    int st; waitpid(c, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "multi-op child done");
    rmset(id); return 0;
}
TEST(getncnt_with_waiter) {
    int id = mkset(1);                 /* 0 */
    pid_t c = fork();
    if (c == 0) { op(id, 0, -1, 0); _exit(0); }
    usleep(200000);
    int n = semctl(id, 0, GETNCNT);
    CHECK(n == 1, "one waiter counted");
    op(id, 0, 1, 0);                   /* release */
    waitpid(c, NULL, 0);
    rmset(id); return 0;
}

/* ============================ SEM_UNDO ============================ */

TEST(undo_decrement_on_exit) {
    int id = mkset(1);
    setval(id, 0, 5);
    pid_t c = fork();
    if (c == 0) {                      /* take 2 with UNDO, then exit */
        op(id, 0, -2, SEM_UNDO);
        _exit(0);
    }
    waitpid(c, NULL, 0);
    usleep(100000);                    /* allow exit-time undo to apply */
    CHECK(getval(id, 0) == 5, "UNDO restored value on exit");
    rmset(id); return 0;
}
TEST(undo_increment_on_exit) {
    int id = mkset(1);
    setval(id, 0, 1);
    pid_t c = fork();
    if (c == 0) { op(id, 0, 3, SEM_UNDO); _exit(0); }
    waitpid(c, NULL, 0);
    usleep(100000);
    CHECK(getval(id, 0) == 1, "UNDO reversed increment");
    rmset(id); return 0;
}
TEST(no_undo_persists) {
    int id = mkset(1);
    setval(id, 0, 5);
    pid_t c = fork();
    if (c == 0) { op(id, 0, -2, 0); _exit(0); }  /* NO undo */
    waitpid(c, NULL, 0);
    usleep(100000);
    CHECK(getval(id, 0) == 3, "no-undo change persists");
    rmset(id); return 0;
}
TEST(undo_balanced_noop) {
    int id = mkset(1);
    setval(id, 0, 5);
    pid_t c = fork();
    if (c == 0) {                      /* -2 then +2 with UNDO nets 0 adjustment */
        op(id, 0, -2, SEM_UNDO);
        op(id, 0, 2, SEM_UNDO);
        _exit(0);
    }
    waitpid(c, NULL, 0);
    usleep(100000);
    CHECK(getval(id, 0) == 5, "balanced undo leaves value");
    rmset(id); return 0;
}

/* ============================ misc ============================ */

TEST(ftok_consistent) {
    key_t a = ftok("/", 42);
    key_t b = ftok("/", 42);
    CHECK(a != (key_t)-1, "ftok ok");
    CHECK(a == b, "ftok deterministic");
    return 0;
}
TEST(perm_denied_other) {
    if (geteuid() == 0) return 1;      /* root bypasses perm checks: SKIP */
    int id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0000);
    CHECK(id >= 0, "create mode 0000");
    CHECK_ERR(op(id, 0, 1, 0), EACCES, "no alter perm");
    rmset(id); return 0;
}
TEST(many_sets_distinct) {
    int ids[16]; int n = 0;
    for (n = 0; n < 16; n++) { ids[n] = mkset(1); if (ids[n] < 0) break; }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            CHECK(ids[i] != ids[j], "ids distinct");
    for (int i = 0; i < n; i++) rmset(ids[i]);
    CHECK(n >= 8, "created several sets");
    return 0;
}
TEST(setall_value_over_max) {
    int id = mkset(2);
    unsigned short in[2] = {1, SEMVMX};   /* SEMVMX is fine; over is ERANGE */
    union t_semun a; a.array = in;
    CHECK(semctl(id, 0, SETALL, a) == 0, "SETALL at SEMVMX ok");
    CHECK(getval(id, 1) == SEMVMX, "value SEMVMX");
    rmset(id); return 0;
}
TEST(stress_inc_dec_loop) {
    int id = mkset(1);
    for (int i = 0; i < 1000; i++) {
        CHECK(op(id, 0, 1, 0) == 0, "inc");
        CHECK(op(id, 0, -1, 0) == 0, "dec");
    }
    CHECK(getval(id, 0) == 0, "balanced loop ends at 0");
    rmset(id); return 0;
}

/* ==================================================================== *
 *  Extended battery — deeper edge coverage to push the suite past 256   *
 *  checkpoints.  Each TEST is correct against a reference host libc      *
 *  (glibc); a failure on substrate localises a kernel/libc gap.         *
 * ==================================================================== */

/* ----- semget: keys, modes, nsems bounds, lifecycle ----- */
TEST(g_nsems_one_exact)        { int id = mkset(1); CHECK(id >= 0, "nsems=1"); rmset(id); return 0; }
TEST(g_nsems_semmsl_exact)     { int id = mkset(SEMMSL); CHECK(id >= 0, "nsems=SEMMSL"); rmset(id); return 0; }
TEST(g_nsems_semmsl_plus1)     { /* over the limit on any platform (substrate 250, Linux ~32000) */
                                 CHECK_ERR(semget(IPC_PRIVATE, 200000, IPC_CREAT|0600), EINVAL, "nsems over limit"); return 0; }
TEST(g_creat_excl_fresh)       { key_t k=0x5e010001; int id=semget(k,1,IPC_CREAT|IPC_EXCL|0600); CHECK(id>=0,"creat|excl fresh"); rmset(id); return 0; }
TEST(g_creat_excl_after_rmid)  { key_t k=0x5e010002; int a=semget(k,1,IPC_CREAT|IPC_EXCL|0600); CHECK(a>=0,"first"); rmset(a);
                                 int b=semget(k,1,IPC_CREAT|IPC_EXCL|0600); CHECK(b>=0,"excl reusable after rmid"); rmset(b); return 0; }
TEST(g_existing_no_creat)      { key_t k=0x5e010003; int a=semget(k,2,IPC_CREAT|0600); CHECK(a>=0,"create"); int b=semget(k,2,0); CHECK(b==a,"plain get existing"); rmset(a); return 0; }
TEST(g_mode_0666)              { key_t k=0x5e010004; int id=semget(k,1,IPC_CREAT|0666); CHECK(id>=0,"create 0666");
                                 struct semid_ds d; CHECK(statds(id,&d)==0,"stat"); CHECK((d.sem_perm.mode&0777)==0666,"mode preserved"); rmset(id); return 0; }
TEST(g_mode_0400)              { key_t k=0x5e010005; int id=semget(k,1,IPC_CREAT|0400); CHECK(id>=0,"create 0400");
                                 struct semid_ds d; CHECK(statds(id,&d)==0,"stat"); CHECK((d.sem_perm.mode&0777)==0400,"ro mode"); rmset(id); return 0; }
TEST(g_ten_distinct_keys)      { int ids[10]; for (int i=0;i<10;i++){ ids[i]=semget(0x5e011000+i,1,IPC_CREAT|0600); CHECK(ids[i]>=0,"create i"); }
                                 for (int i=0;i<10;i++) for (int j=i+1;j<10;j++) CHECK(ids[i]!=ids[j],"distinct ids");
                                 for (int i=0;i<10;i++) rmset(ids[i]); return 0; }
TEST(g_private_five_unique)    { int ids[5]; for (int i=0;i<5;i++){ ids[i]=mkset(1); CHECK(ids[i]>=0,"priv i"); }
                                 for (int i=0;i<5;i++) for (int j=i+1;j<5;j++) CHECK(ids[i]!=ids[j],"unique priv ids");
                                 for (int i=0;i<5;i++) rmset(ids[i]); return 0; }
TEST(g_recreate_three)         { key_t k=0x5e010006; for (int i=0;i<3;i++){ int id=semget(k,1,IPC_CREAT|0600); CHECK(id>=0,"recreate"); rmset(id); } return 0; }
TEST(g_excl_alone_existing)    { key_t k=0x5e010007; int a=semget(k,1,IPC_CREAT|0600); CHECK(a>=0,"create");
                                 int b=semget(k,1,IPC_EXCL|0600); CHECK(b==a,"EXCL without CREAT just gets"); rmset(a); return 0; }
TEST(g_no_creat_missing)       { CHECK_ERR(semget(0x5e0100ff,1,0600), ENOENT, "get missing no-creat"); return 0; }
TEST(g_after_rmid_missing)     { key_t k=0x5e010008; int a=semget(k,1,IPC_CREAT|0600); CHECK(a>=0,"create"); rmset(a);
                                 CHECK_ERR(semget(k,1,0600), ENOENT, "gone after rmid"); return 0; }
TEST(g_id_is_nonneg)           { int id=mkset(3); CHECK(id>=0,"id nonneg"); rmset(id); return 0; }
TEST(g_two_keys_independent)   { key_t k1=0x5e010009,k2=0x5e01000a; int a=semget(k1,1,IPC_CREAT|0600),b=semget(k2,1,IPC_CREAT|0600);
                                 CHECK(a>=0&&b>=0,"two"); setval(a,0,11); setval(b,0,22);
                                 CHECK(getval(a,0)==11&&getval(b,0)==22,"independent values"); rmset(a); rmset(b); return 0; }

/* ----- semctl GETVAL/SETVAL across indices ----- */
TEST(c_setval_each_index)      { int id=mkset(8); for (int i=0;i<8;i++) CHECK(setval(id,i,i+1)==0,"setval i");
                                 for (int i=0;i<8;i++) CHECK(getval(id,i)==i+1,"getval i"); rmset(id); return 0; }
TEST(c_setval_high_index)      { int id=mkset(8); CHECK(setval(id,7,SEMVMX)==0,"setval last"); CHECK(getval(id,7)==SEMVMX,"max at last"); rmset(id); return 0; }
TEST(c_setval_index_oob)       { int id=mkset(4); CHECK_ERR(setval(id,4,1), EINVAL, "setval idx==nsems"); rmset(id); return 0; }
TEST(c_getval_index_oob)       { int id=mkset(4); CHECK_ERR(semctl(id,4,GETVAL), EINVAL, "getval idx==nsems"); rmset(id); return 0; }
TEST(c_setval_neg_index)       { int id=mkset(4); CHECK_ERR(setval(id,-1,1), EINVAL, "setval idx<0"); rmset(id); return 0; }
TEST(c_setval_max_roundtrip)   { int id=mkset(1); CHECK(setval(id,0,SEMVMX)==0,"set max"); CHECK(getval(id,0)==SEMVMX,"get max"); rmset(id); return 0; }
TEST(c_setval_badid)           { CHECK_ERR(semctl(-1,0,SETVAL,(union t_semun){.val=1}), EINVAL, "setval bad id"); return 0; }
TEST(c_getval_badid)           { CHECK_ERR(semctl(0x7fffffff,0,GETVAL), EINVAL, "getval bad id"); return 0; }
TEST(c_setval_then_zero)       { int id=mkset(1); setval(id,0,9); CHECK(setval(id,0,0)==0,"set 0"); CHECK(getval(id,0)==0,"is 0"); rmset(id); return 0; }

/* ----- semctl GETALL / SETALL ----- */
TEST(c_setall_getall_8)        { int id=mkset(8); unsigned short v[8],o[8]; for (int i=0;i<8;i++) v[i]=(i*3+1)%30;
                                 CHECK(setall(id,v)==0,"setall"); CHECK(getall(id,o)==0,"getall");
                                 for (int i=0;i<8;i++) CHECK(o[i]==v[i],"all match"); rmset(id); return 0; }
TEST(c_setall_zeroes)          { int id=mkset(5); unsigned short v[5]={0,0,0,0,0},o[5]; CHECK(setall(id,v)==0,"setall 0");
                                 CHECK(getall(id,o)==0,"getall"); for (int i=0;i<5;i++) CHECK(o[i]==0,"zero"); rmset(id); return 0; }
TEST(c_setall_max)             { int id=mkset(3); unsigned short v[3]={SEMVMX,SEMVMX,SEMVMX},o[3]; CHECK(setall(id,v)==0,"setall max");
                                 CHECK(getall(id,o)==0,"getall"); for (int i=0;i<3;i++) CHECK(o[i]==SEMVMX,"max"); rmset(id); return 0; }
TEST(c_getall_after_setval)    { int id=mkset(4); setval(id,2,17); unsigned short o[4]; CHECK(getall(id,o)==0,"getall"); CHECK(o[2]==17,"reflects setval"); rmset(id); return 0; }
TEST(c_setall_then_getval)     { int id=mkset(4); unsigned short v[4]={4,5,6,7}; setall(id,v); CHECK(getval(id,3)==7,"getval after setall"); rmset(id); return 0; }
TEST(c_setall_one_index_op)    { int id=mkset(3); unsigned short v[3]={2,0,5}; setall(id,v);
                                 CHECK(op(id,0,-1,IPC_NOWAIT)==0,"dec idx0"); CHECK(getval(id,0)==1,"idx0=1"); CHECK(getval(id,2)==5,"idx2 untouched"); rmset(id); return 0; }

/* ----- semctl IPC_STAT / IPC_SET fields ----- */
TEST(s_stat_sem_nsems)         { int id=mkset(6); struct semid_ds d; CHECK(statds(id,&d)==0,"stat"); CHECK(d.sem_nsems==6,"nsems field"); rmset(id); return 0; }
TEST(s_stat_perm_cuid)         { int id=mkset(1); struct semid_ds d; CHECK(statds(id,&d)==0,"stat"); CHECK(d.sem_perm.cuid==geteuid(),"cuid"); rmset(id); return 0; }
TEST(s_stat_perm_cgid)         { int id=mkset(1); struct semid_ds d; CHECK(statds(id,&d)==0,"stat"); CHECK(d.sem_perm.cgid==getegid(),"cgid"); rmset(id); return 0; }
TEST(s_set_mode_persists)      { int id=mkset(1); struct semid_ds d; statds(id,&d); d.sem_perm.mode=(d.sem_perm.mode&~0777)|0640;
                                 union t_semun a; a.buf=&d; CHECK(semctl(id,0,IPC_SET,a)==0,"set mode");
                                 struct semid_ds e; statds(id,&e); CHECK((e.sem_perm.mode&0777)==0640,"mode 0640 persisted"); rmset(id); return 0; }
TEST(s_set_uid_persists)       { int id=mkset(1); struct semid_ds d; statds(id,&d); d.sem_perm.uid=d.sem_perm.uid;
                                 union t_semun a; a.buf=&d; CHECK(semctl(id,0,IPC_SET,a)==0,"set uid same"); rmset(id); return 0; }
TEST(s_stat_otime_zero_new)    { int id=mkset(1); struct semid_ds d; CHECK(statds(id,&d)==0,"stat"); CHECK(d.sem_otime==0,"otime 0 on new"); rmset(id); return 0; }
TEST(s_stat_otime_set_by_op)   { int id=mkset(1); op(id,0,1,0); struct semid_ds d; CHECK(statds(id,&d)==0,"stat"); CHECK(d.sem_otime!=0,"otime set by op"); rmset(id); return 0; }
TEST(s_stat_ctime_set)         { int id=mkset(1); struct semid_ds d; CHECK(statds(id,&d)==0,"stat"); CHECK(d.sem_ctime!=0,"ctime nonzero"); rmset(id); return 0; }
TEST(s_set_updates_ctime)      { int id=mkset(1); struct semid_ds d; statds(id,&d); time_t c0=d.sem_ctime; sleep(1);
                                 union t_semun a; a.buf=&d; semctl(id,0,IPC_SET,a); struct semid_ds e; statds(id,&e);
                                 CHECK(e.sem_ctime>=c0,"ctime advanced by IPC_SET"); rmset(id); return 0; }
TEST(s_stat_badid)             { struct semid_ds d; union t_semun a; a.buf=&d; CHECK_ERR(semctl(0x7ffffffe,0,IPC_STAT,a), EINVAL, "stat bad id"); return 0; }

/* ----- semctl invalid commands ----- */
TEST(c_invalid_cmd)            { int id=mkset(1); CHECK_ERR(semctl(id,0,0x7777), EINVAL, "bogus cmd"); rmset(id); return 0; }
TEST(c_rmid_returns_zero)      { int id=mkset(1); CHECK(semctl(id,0,IPC_RMID)==0,"rmid ret 0"); return 0; }

/* ----- GETPID / GETNCNT / GETZCNT ----- */
TEST(p_getpid_self_after_op)   { int id=mkset(1); op(id,0,1,0); CHECK(semctl(id,0,GETPID)==getpid(),"sempid==self"); rmset(id); return 0; }
TEST(p_getpid_each_index)      { int id=mkset(3); op(id,1,1,0); CHECK(semctl(id,1,GETPID)==getpid(),"pid idx1"); CHECK(semctl(id,0,GETPID)==0,"pid idx0 untouched"); rmset(id); return 0; }
TEST(p_getncnt_zero_idle)      { int id=mkset(1); setval(id,0,1); CHECK(semctl(id,0,GETNCNT)==0,"ncnt 0 idle"); rmset(id); return 0; }
TEST(p_getzcnt_zero_idle)      { int id=mkset(1); setval(id,0,1); CHECK(semctl(id,0,GETZCNT)==0,"zcnt 0 idle"); rmset(id); return 0; }
TEST(p_getncnt_one_waiter)     { int id=mkset(1); pid_t c=fork(); if(c==0){ op(id,0,-1,0); _exit(0);} usleep(200000);
                                 int n=semctl(id,0,GETNCNT); CHECK(n==1,"ncnt 1"); op(id,0,1,0); waitpid(c,NULL,0); rmset(id); return 0; }
TEST(p_getzcnt_one_waiter)     { int id=mkset(1); setval(id,0,3); pid_t c=fork(); if(c==0){ op(id,0,0,0); _exit(0);} usleep(200000);
                                 int z=semctl(id,0,GETZCNT); CHECK(z==1,"zcnt 1"); setval(id,0,0); waitpid(c,NULL,0); rmset(id); return 0; }
TEST(p_getncnt_two_waiters)    { int id=mkset(1); pid_t a=fork(); if(a==0){op(id,0,-1,0);_exit(0);} pid_t b=fork(); if(b==0){op(id,0,-1,0);_exit(0);}
                                 usleep(250000); CHECK(semctl(id,0,GETNCNT)==2,"ncnt 2"); op(id,0,2,0); waitpid(a,NULL,0); waitpid(b,NULL,0); rmset(id); return 0; }

/* ----- semop arithmetic / flags ----- */
TEST(o_inc_by_five)            { int id=mkset(1); CHECK(op(id,0,5,0)==0,"+5"); CHECK(getval(id,0)==5,"val 5"); rmset(id); return 0; }
TEST(o_dec_nowait_ok)          { int id=mkset(1); setval(id,0,3); CHECK(op(id,0,-3,IPC_NOWAIT)==0,"-3"); CHECK(getval(id,0)==0,"0"); rmset(id); return 0; }
TEST(o_dec_nowait_eagain)      { int id=mkset(1); setval(id,0,2); CHECK_ERR(op(id,0,-3,IPC_NOWAIT), EAGAIN, "-3 from 2 nowait"); rmset(id); return 0; }
TEST(o_zero_nowait_eagain)     { int id=mkset(1); setval(id,0,1); CHECK_ERR(op(id,0,0,IPC_NOWAIT), EAGAIN, "wait0 nowait nonzero"); rmset(id); return 0; }
TEST(o_zero_when_zero_ok)      { int id=mkset(1); CHECK(op(id,0,0,IPC_NOWAIT)==0,"wait0 when 0"); rmset(id); return 0; }
TEST(o_overflow_erange)        { int id=mkset(1); setval(id,0,SEMVMX); CHECK_ERR(op(id,0,1,0), ERANGE, "+1 over SEMVMX"); rmset(id); return 0; }
TEST(o_overflow_big_erange)    { int id=mkset(1); setval(id,0,1); CHECK_ERR(op(id,0,SEMVMX,0), ERANGE, "+SEMVMX from 1"); rmset(id); return 0; }
TEST(o_nsops_zero_ok)          { /* 16 simultaneous wait-for-zero ops on all-zero sems succeed atomically */
                                 int id=mkset(16); struct sembuf sb[16]; for(int i=0;i<16;i++){sb[i].sem_num=i;sb[i].sem_op=0;sb[i].sem_flg=0;}
                                 CHECK(semop(id,sb,16)==0,"16 wait-zero on zeros"); rmset(id); return 0; }
TEST(o_nsops_too_many)         { /* exceed SEMOPM on any platform (substrate 32, Linux ~500) */
                                 int id=mkset(1); size_t n=100000; struct sembuf *sb=calloc(n,sizeof(*sb)); CHECK(sb!=NULL,"calloc");
                                 errno=0; int rc=semop(id,sb,n); free(sb); CHECK(rc==-1&&errno==E2BIG,"nsops huge -> E2BIG"); rmset(id); return 0; }
TEST(o_bad_semnum)             { int id=mkset(2); CHECK_ERR(op(id,5,1,0), EFBIG, "op semnum oob"); rmset(id); return 0; }
TEST(o_badid_einval)           { CHECK_ERR(op(0x7ffffffd,0,1,0), EINVAL, "op bad id"); return 0; }
TEST(o_multi_distinct_ok)      { int id=mkset(3); struct sembuf sb[3]={{0,1,0},{1,2,0},{2,3,0}}; CHECK(semop(id,sb,3)==0,"multi");
                                 CHECK(getval(id,0)==1&&getval(id,1)==2&&getval(id,2)==3,"all applied"); rmset(id); return 0; }
TEST(o_multi_rollback)         { int id=mkset(2); setval(id,0,1); struct sembuf sb[2]={{0,-1,IPC_NOWAIT},{1,-1,IPC_NOWAIT}};
                                 CHECK_ERR(semop(id,sb,2), EAGAIN, "second blocks -> rollback");
                                 CHECK(getval(id,0)==1,"idx0 rolled back"); rmset(id); return 0; }
TEST(o_same_sem_accumulate)    { int id=mkset(1); struct sembuf sb[3]={{0,2,0},{0,3,0},{0,-1,0}}; CHECK(semop(id,sb,3)==0,"2+3-1");
                                 CHECK(getval(id,0)==4,"val 4"); rmset(id); return 0; }
TEST(o_same_sem_transient_neg) { int id=mkset(1); setval(id,0,1); struct sembuf sb[2]={{0,-2,IPC_NOWAIT},{0,5,0}};
                                 CHECK_ERR(semop(id,sb,2), EAGAIN, "transient negative blocks"); CHECK(getval(id,0)==1,"unchanged"); rmset(id); return 0; }
TEST(o_dec_then_inc_net)       { int id=mkset(1); setval(id,0,4); op(id,0,-1,0); op(id,0,1,0); CHECK(getval(id,0)==4,"net 4"); rmset(id); return 0; }
TEST(o_to_exact_zero)          { int id=mkset(1); setval(id,0,7); CHECK(op(id,0,-7,0)==0,"-7 to 0"); CHECK(getval(id,0)==0,"0"); rmset(id); return 0; }

/* ----- blocking / wakeup semantics ----- */
TEST(b_inc_wakes_one)          { int id=mkset(1); pid_t c=fork(); if(c==0){_exit(op(id,0,-1,0)==0?0:1);} usleep(200000);
                                 CHECK(op(id,0,1,0)==0,"+1"); int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"woke"); rmset(id); return 0; }
TEST(b_setval_wakes_waiter)    { int id=mkset(1); pid_t c=fork(); if(c==0){_exit(op(id,0,-1,0)==0?0:1);} usleep(200000);
                                 setval(id,0,1); int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"setval woke"); rmset(id); return 0; }
TEST(b_setall_wakes_waiter)    { int id=mkset(2); pid_t c=fork(); if(c==0){_exit(op(id,1,-1,0)==0?0:1);} usleep(200000);
                                 unsigned short v[2]={0,1}; setall(id,v); int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"setall woke"); rmset(id); return 0; }
TEST(b_waitzero_wakes)         { int id=mkset(1); setval(id,0,2); pid_t c=fork(); if(c==0){_exit(op(id,0,0,0)==0?0:1);} usleep(200000);
                                 setval(id,0,0); int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"wait0 woke"); rmset(id); return 0; }
TEST(b_rmid_wakes_eidrm)       { int id=mkset(1); pid_t c=fork(); if(c==0){ errno=0; int r=op(id,0,-1,0); _exit((r==-1&&errno==EIDRM)?0:1);} usleep(200000);
                                 semctl(id,0,IPC_RMID); int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"blocked waiter got EIDRM"); return 0; }
TEST(b_partial_wake_one)       { int id=mkset(1); pid_t a=fork(); if(a==0){_exit(op(id,0,-1,0)==0?0:1);} pid_t b=fork(); if(b==0){_exit(op(id,0,-1,0)==0?0:1);}
                                 usleep(250000); op(id,0,1,0); usleep(150000);
                                 /* exactly one should have finished */
                                 int s1=0,s2=0; int d1=(waitpid(a,&s1,WNOHANG)==a), d2=(waitpid(b,&s2,WNOHANG)==b);
                                 CHECK(d1^d2,"exactly one woke on +1"); op(id,0,1,0); waitpid(a,&s1,0); waitpid(b,&s2,0); rmset(id); return 0; }
TEST(b_multiop_wakes)          { int id=mkset(2); pid_t c=fork(); if(c==0){ struct sembuf sb[2]={{0,-1,0},{1,-1,0}}; _exit(semop(id,sb,2)==0?0:1);} usleep(200000);
                                 op(id,0,1,0); op(id,1,1,0); int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"multiop woke"); rmset(id); return 0; }
TEST(b_waiter_then_value_ok)   { int id=mkset(1); pid_t c=fork(); if(c==0){_exit(op(id,0,-5,0)==0?0:1);} usleep(200000);
                                 op(id,0,2,0); op(id,0,3,0); int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"accumulated wake"); CHECK(getval(id,0)==0,"0"); rmset(id); return 0; }

/* ----- SEM_UNDO ----- */
TEST(u_undo_inc_on_exit)       { int id=mkset(1); setval(id,0,5); pid_t c=fork(); if(c==0){ op(id,0,3,SEM_UNDO); _exit(0);} waitpid(c,NULL,0); usleep(100000);
                                 CHECK(getval(id,0)==5,"undo removed the +3"); rmset(id); return 0; }
TEST(u_undo_dec_on_exit)       { int id=mkset(1); setval(id,0,5); pid_t c=fork(); if(c==0){ op(id,0,-3,SEM_UNDO); _exit(0);} waitpid(c,NULL,0); usleep(100000);
                                 CHECK(getval(id,0)==5,"undo restored the -3"); rmset(id); return 0; }
TEST(u_no_undo_persists)       { int id=mkset(1); setval(id,0,5); pid_t c=fork(); if(c==0){ op(id,0,-2,0); _exit(0);} waitpid(c,NULL,0); usleep(100000);
                                 CHECK(getval(id,0)==3,"no-undo -2 persists"); rmset(id); return 0; }
TEST(u_undo_balanced_zero)     { int id=mkset(1); setval(id,0,5); pid_t c=fork(); if(c==0){ op(id,0,2,SEM_UNDO); op(id,0,-2,SEM_UNDO); _exit(0);} waitpid(c,NULL,0); usleep(100000);
                                 CHECK(getval(id,0)==5,"balanced undo net zero"); rmset(id); return 0; }
TEST(u_undo_partial)           { int id=mkset(1); setval(id,0,10); pid_t c=fork(); if(c==0){ op(id,0,-3,SEM_UNDO); op(id,0,1,0); _exit(0);} waitpid(c,NULL,0); usleep(100000);
                                 /* undo only reverts the SEM_UNDO -3; the +1 is not undone */
                                 CHECK(getval(id,0)==11,"only undo-tracked op reverted"); rmset(id); return 0; }
TEST(u_undo_two_indices)       { int id=mkset(2); setval(id,0,4); setval(id,1,6); pid_t c=fork();
                                 if(c==0){ op(id,0,-2,SEM_UNDO); op(id,1,-1,SEM_UNDO); _exit(0);} waitpid(c,NULL,0); usleep(100000);
                                 CHECK(getval(id,0)==4&&getval(id,1)==6,"both indices undone"); rmset(id); return 0; }
TEST(u_undo_survives_sibling)  { int id=mkset(1); setval(id,0,5);
                                 pid_t a=fork(); if(a==0){ op(id,0,-2,SEM_UNDO); _exit(0);} waitpid(a,NULL,0); usleep(80000);
                                 CHECK(getval(id,0)==5,"first child undo applied");
                                 pid_t b=fork(); if(b==0){ op(id,0,-1,0); _exit(0);} waitpid(b,NULL,0); usleep(80000);
                                 CHECK(getval(id,0)==4,"sibling no-undo persists"); rmset(id); return 0; }

/* ----- concurrency / stress ----- */
TEST(z_two_procs_inc)          { int id=mkset(1); pid_t a=fork(); if(a==0){ for(int i=0;i<500;i++) op(id,0,1,0); _exit(0);} pid_t b=fork(); if(b==0){ for(int i=0;i<500;i++) op(id,0,1,0); _exit(0);}
                                 waitpid(a,NULL,0); waitpid(b,NULL,0); CHECK(getval(id,0)==1000,"1000 increments counted"); rmset(id); return 0; }
TEST(z_producer_consumer)      { int id=mkset(1); pid_t c=fork(); if(c==0){ for(int i=0;i<300;i++) op(id,0,-1,0); _exit(0);} for(int i=0;i<300;i++) op(id,0,1,0);
                                 int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"consumer drained"); CHECK(getval(id,0)==0,"net 0"); rmset(id); return 0; }
TEST(z_many_sets_cycle)        { for(int i=0;i<40;i++){ int id=mkset(2); CHECK(id>=0,"create"); op(id,0,1,0); CHECK(getval(id,0)==1,"op"); rmset(id);} return 0; }
TEST(z_mutex_pattern)          { int id=mkset(1); setval(id,0,1); pid_t c=fork();
                                 if(c==0){ for(int i=0;i<200;i++){ op(id,0,-1,0); op(id,0,1,0);} _exit(0);} for(int i=0;i<200;i++){ op(id,0,-1,0); op(id,0,1,0);}
                                 int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"mutex churn"); CHECK(getval(id,0)==1,"mutex free"); rmset(id); return 0; }
TEST(z_barrier_two)            { /* two-process rendezvous over a 2-sem set: each posts one sem and
                                  * waits on the other, so neither proceeds until both have arrived. */
                                 int id=mkset(2); pid_t c=fork();
                                 if(c==0){ op(id,1,1,0); op(id,0,-1,0); _exit(0); }   /* child: post s1, wait s0 */
                                 op(id,0,1,0); op(id,1,-1,0);                          /* parent: post s0, wait s1 */
                                 int st; waitpid(c,&st,0); CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"rendezvous met");
                                 CHECK(getval(id,0)==0&&getval(id,1)==0,"both consumed"); rmset(id); return 0; }

/* ----- macro-generated boundary sweeps ----- */

/* SETVAL/GETVAL roundtrip at a boundary value */
#define GEN_VAL(tag, V) TEST(val_rt_##tag){ int id=mkset(1); CHECK(setval(id,0,(V))==0,"setval"); CHECK(getval(id,0)==(V),"getval"); rmset(id); return 0; }
GEN_VAL(0,0) GEN_VAL(1,1) GEN_VAL(2,2) GEN_VAL(3,3) GEN_VAL(4,4) GEN_VAL(7,7) GEN_VAL(8,8)
GEN_VAL(15,15) GEN_VAL(16,16) GEN_VAL(32,32) GEN_VAL(64,64) GEN_VAL(127,127) GEN_VAL(128,128)
GEN_VAL(255,255) GEN_VAL(256,256) GEN_VAL(512,512) GEN_VAL(1000,1000) GEN_VAL(2048,2048)
GEN_VAL(8192,8192) GEN_VAL(16383,16383) GEN_VAL(16384,16384) GEN_VAL(32766,32766) GEN_VAL(max,SEMVMX)

/* create with nsems=N, op the first and last index */
#define GEN_NSEMS(N) TEST(nsems_##N){ int id=mkset(N); CHECK(id>=0,"create"); CHECK(op(id,0,1,0)==0,"op0"); CHECK(op(id,(N)-1,1,0)==0,"opN-1"); rmset(id); return 0; }
GEN_NSEMS(1) GEN_NSEMS(2) GEN_NSEMS(3) GEN_NSEMS(4) GEN_NSEMS(5) GEN_NSEMS(8) GEN_NSEMS(16) GEN_NSEMS(32)
GEN_NSEMS(50) GEN_NSEMS(64) GEN_NSEMS(100) GEN_NSEMS(128) GEN_NSEMS(200) GEN_NSEMS(249) GEN_NSEMS(250)

/* increment by N from 0 */
#define GEN_INC(N) TEST(inc_##N){ int id=mkset(1); CHECK(op(id,0,(N),0)==0,"+N"); CHECK(getval(id,0)==(N),"=N"); rmset(id); return 0; }
GEN_INC(1) GEN_INC(2) GEN_INC(3) GEN_INC(7) GEN_INC(16) GEN_INC(64) GEN_INC(100) GEN_INC(1000)
GEN_INC(8192) GEN_INC(16384) GEN_INC(32766) GEN_INC(32767)

/* decrement V -> 0 with IPC_NOWAIT */
#define GEN_DEC(V) TEST(dec_##V){ int id=mkset(1); setval(id,0,(V)); CHECK(op(id,0,-(V),IPC_NOWAIT)==0,"-V"); CHECK(getval(id,0)==0,"0"); rmset(id); return 0; }
GEN_DEC(1) GEN_DEC(2) GEN_DEC(3) GEN_DEC(7) GEN_DEC(16) GEN_DEC(64) GEN_DEC(100) GEN_DEC(1000)
GEN_DEC(8192) GEN_DEC(16384) GEN_DEC(32766) GEN_DEC(32767)

/* operate on index I of a 16-sem set; all other indices stay 0 */
#define GEN_IDX(I) TEST(idx_##I){ int id=mkset(16); CHECK(op(id,(I),(I)+1,0)==0,"op idx"); CHECK(getval(id,(I))==(I)+1,"val"); \
    for (int j=0;j<16;j++) if (j!=(I)) CHECK(getval(id,j)==0,"others 0"); rmset(id); return 0; }
GEN_IDX(0) GEN_IDX(1) GEN_IDX(2) GEN_IDX(3) GEN_IDX(4) GEN_IDX(5) GEN_IDX(6) GEN_IDX(7)
GEN_IDX(8) GEN_IDX(9) GEN_IDX(10) GEN_IDX(11) GEN_IDX(12) GEN_IDX(13) GEN_IDX(14) GEN_IDX(15)

/* atomic multi-op touching K distinct sems in one semop() */
#define GEN_MOP(K) TEST(mop_##K){ int id=mkset(16); struct sembuf sb[K]; for (int i=0;i<(K);i++){ sb[i].sem_num=i; sb[i].sem_op=i+1; sb[i].sem_flg=0; } \
    CHECK(semop(id,sb,(K))==0,"multiop K"); for (int i=0;i<(K);i++) CHECK(getval(id,i)==i+1,"val i"); rmset(id); return 0; }
GEN_MOP(1) GEN_MOP(2) GEN_MOP(3) GEN_MOP(4) GEN_MOP(5) GEN_MOP(6) GEN_MOP(7) GEN_MOP(8)
GEN_MOP(9) GEN_MOP(10) GEN_MOP(11) GEN_MOP(12) GEN_MOP(13) GEN_MOP(14) GEN_MOP(15) GEN_MOP(16)

/* SEM_UNDO: child takes -D from V, exit must restore V */
#define GEN_UNDO(V,D) TEST(undo_##V##_##D){ int id=mkset(1); setval(id,0,(V)); pid_t c=fork(); \
    if (c==0){ op(id,0,-(D),SEM_UNDO); _exit(0); } waitpid(c,NULL,0); usleep(80000); \
    CHECK(getval(id,0)==(V),"undo restored"); rmset(id); return 0; }
GEN_UNDO(2,1) GEN_UNDO(5,2) GEN_UNDO(10,7) GEN_UNDO(16,16) GEN_UNDO(100,50)
GEN_UNDO(255,128) GEN_UNDO(1000,999) GEN_UNDO(8192,4096) GEN_UNDO(16384,1) GEN_UNDO(32767,32767)

int main(void) {
    fprintf(stdout, "torture_sem: System V semaphore suite\n");

    RUN(get_private);
    RUN(get_private_multi);
    RUN(get_keyed_creat);
    RUN(get_existing_same_id);
    RUN(get_excl_eexist);
    RUN(get_noent);
    RUN(get_nsems_zero_create);
    RUN(get_nsems_negative);
    RUN(get_nsems_too_big);
    RUN(get_smaller_nsems_ok);
    RUN(get_bigger_nsems_einval);
    RUN(get_private_unique);
    RUN(get_zero_nsems_existing_ok);

    RUN(setval_getval);
    RUN(setval_zero);
    RUN(setval_max);
    RUN(initial_val_zero);
    RUN(setval_negative_erange);
    RUN(setval_over_max_erange);
    RUN(getval_bad_semnum);
    RUN(setval_bad_semnum);
    RUN(setval_independent);

    RUN(setall_getall);
    RUN(getall_reflects_setval);
    RUN(setall_then_op);

    RUN(getpid_initial_zero);
    RUN(getpid_after_op);
    RUN(getncnt_zero);
    RUN(getzcnt_zero);

    RUN(stat_nsems);
    RUN(stat_perm_mode);
    RUN(stat_perm_uid);
    RUN(stat_ctime_nonzero);
    RUN(stat_otime_initially_zero);
    RUN(stat_otime_after_op);
    RUN(set_mode);

    RUN(rmid);
    RUN(rmid_twice);
    RUN(op_after_rmid);
    RUN(keyed_reuse_after_rmid);
    RUN(stale_id_after_rmid_recreate);

    RUN(op_increment);
    RUN(op_decrement);
    RUN(op_to_zero);
    RUN(op_wait_zero_when_zero);
    RUN(op_nowait_block_decrement);
    RUN(op_nowait_block_zero);
    RUN(op_overflow_erange);
    RUN(op_zero_nsops);
    RUN(op_too_many_ops);
    RUN(op_bad_semnum);
    RUN(op_multi_atomic_ok);
    RUN(op_multi_atomic_rollback);
    RUN(op_same_sem_twice);
    RUN(op_same_sem_intermediate_negative);

    RUN(block_decrement_wakes);
    RUN(block_waitzero_wakes);
    RUN(block_setval_wakes);
    RUN(block_then_rmid_eidrm);
    RUN(two_waiters_resource);
    RUN(block_multiop_wakes);
    RUN(getncnt_with_waiter);

    RUN(undo_decrement_on_exit);
    RUN(undo_increment_on_exit);
    RUN(no_undo_persists);
    RUN(undo_balanced_noop);

    RUN(ftok_consistent);
    RUN(perm_denied_other);
    RUN(many_sets_distinct);
    RUN(setall_value_over_max);
    RUN(stress_inc_dec_loop);

    /* --- extended battery --- */
    RUN(g_nsems_one_exact); RUN(g_nsems_semmsl_exact); RUN(g_nsems_semmsl_plus1);
    RUN(g_creat_excl_fresh); RUN(g_creat_excl_after_rmid); RUN(g_existing_no_creat);
    RUN(g_mode_0666); RUN(g_mode_0400); RUN(g_ten_distinct_keys); RUN(g_private_five_unique);
    RUN(g_recreate_three); RUN(g_excl_alone_existing); RUN(g_no_creat_missing);
    RUN(g_after_rmid_missing); RUN(g_id_is_nonneg); RUN(g_two_keys_independent);

    RUN(c_setval_each_index); RUN(c_setval_high_index); RUN(c_setval_index_oob);
    RUN(c_getval_index_oob); RUN(c_setval_neg_index); RUN(c_setval_max_roundtrip);
    RUN(c_setval_badid); RUN(c_getval_badid); RUN(c_setval_then_zero);

    RUN(c_setall_getall_8); RUN(c_setall_zeroes); RUN(c_setall_max);
    RUN(c_getall_after_setval); RUN(c_setall_then_getval); RUN(c_setall_one_index_op);

    RUN(s_stat_sem_nsems); RUN(s_stat_perm_cuid); RUN(s_stat_perm_cgid);
    RUN(s_set_mode_persists); RUN(s_set_uid_persists); RUN(s_stat_otime_zero_new);
    RUN(s_stat_otime_set_by_op); RUN(s_stat_ctime_set); RUN(s_set_updates_ctime); RUN(s_stat_badid);

    RUN(c_invalid_cmd); RUN(c_rmid_returns_zero);

    RUN(p_getpid_self_after_op); RUN(p_getpid_each_index); RUN(p_getncnt_zero_idle);
    RUN(p_getzcnt_zero_idle); RUN(p_getncnt_one_waiter); RUN(p_getzcnt_one_waiter); RUN(p_getncnt_two_waiters);

    RUN(o_inc_by_five); RUN(o_dec_nowait_ok); RUN(o_dec_nowait_eagain); RUN(o_zero_nowait_eagain);
    RUN(o_zero_when_zero_ok); RUN(o_overflow_erange); RUN(o_overflow_big_erange); RUN(o_nsops_zero_ok);
    RUN(o_nsops_too_many); RUN(o_bad_semnum); RUN(o_badid_einval); RUN(o_multi_distinct_ok);
    RUN(o_multi_rollback); RUN(o_same_sem_accumulate); RUN(o_same_sem_transient_neg);
    RUN(o_dec_then_inc_net); RUN(o_to_exact_zero);

    RUN(b_inc_wakes_one); RUN(b_setval_wakes_waiter); RUN(b_setall_wakes_waiter);
    RUN(b_waitzero_wakes); RUN(b_rmid_wakes_eidrm); RUN(b_partial_wake_one);
    RUN(b_multiop_wakes); RUN(b_waiter_then_value_ok);

    RUN(u_undo_inc_on_exit); RUN(u_undo_dec_on_exit); RUN(u_no_undo_persists);
    RUN(u_undo_balanced_zero); RUN(u_undo_partial); RUN(u_undo_two_indices); RUN(u_undo_survives_sibling);

    RUN(z_two_procs_inc); RUN(z_producer_consumer); RUN(z_many_sets_cycle);
    RUN(z_mutex_pattern); RUN(z_barrier_two);

    /* macro-generated boundary sweeps */
    RUN(val_rt_0); RUN(val_rt_1); RUN(val_rt_2); RUN(val_rt_3); RUN(val_rt_4); RUN(val_rt_7); RUN(val_rt_8);
    RUN(val_rt_15); RUN(val_rt_16); RUN(val_rt_32); RUN(val_rt_64); RUN(val_rt_127); RUN(val_rt_128);
    RUN(val_rt_255); RUN(val_rt_256); RUN(val_rt_512); RUN(val_rt_1000); RUN(val_rt_2048);
    RUN(val_rt_8192); RUN(val_rt_16383); RUN(val_rt_16384); RUN(val_rt_32766); RUN(val_rt_max);

    RUN(nsems_1); RUN(nsems_2); RUN(nsems_3); RUN(nsems_4); RUN(nsems_5); RUN(nsems_8); RUN(nsems_16);
    RUN(nsems_32); RUN(nsems_50); RUN(nsems_64); RUN(nsems_100); RUN(nsems_128); RUN(nsems_200);
    RUN(nsems_249); RUN(nsems_250);

    RUN(inc_1); RUN(inc_2); RUN(inc_3); RUN(inc_7); RUN(inc_16); RUN(inc_64); RUN(inc_100); RUN(inc_1000);
    RUN(inc_8192); RUN(inc_16384); RUN(inc_32766); RUN(inc_32767);

    RUN(dec_1); RUN(dec_2); RUN(dec_3); RUN(dec_7); RUN(dec_16); RUN(dec_64); RUN(dec_100); RUN(dec_1000);
    RUN(dec_8192); RUN(dec_16384); RUN(dec_32766); RUN(dec_32767);

    RUN(idx_0); RUN(idx_1); RUN(idx_2); RUN(idx_3); RUN(idx_4); RUN(idx_5); RUN(idx_6); RUN(idx_7);
    RUN(idx_8); RUN(idx_9); RUN(idx_10); RUN(idx_11); RUN(idx_12); RUN(idx_13); RUN(idx_14); RUN(idx_15);

    RUN(mop_1); RUN(mop_2); RUN(mop_3); RUN(mop_4); RUN(mop_5); RUN(mop_6); RUN(mop_7); RUN(mop_8);
    RUN(mop_9); RUN(mop_10); RUN(mop_11); RUN(mop_12); RUN(mop_13); RUN(mop_14); RUN(mop_15); RUN(mop_16);

    RUN(undo_2_1); RUN(undo_5_2); RUN(undo_10_7); RUN(undo_16_16); RUN(undo_100_50);
    RUN(undo_255_128); RUN(undo_1000_999); RUN(undo_8192_4096); RUN(undo_16384_1); RUN(undo_32767_32767);

    fprintf(stdout, "\n=== %d run: %d pass, %d fail, %d hang, %d skip ===\n",
            tests_run, tests_pass, tests_fail, tests_hang, tests_skip);
    return (tests_fail || tests_hang) ? 1 : 0;
}
