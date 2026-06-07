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

    fprintf(stdout, "\n=== %d run: %d pass, %d fail, %d hang, %d skip ===\n",
            tests_run, tests_pass, tests_fail, tests_hang, tests_skip);
    return (tests_fail || tests_hang) ? 1 : 0;
}
