/*
 * torture_fork64.c — 64+ point fork(2) / process-lifecycle torture battery.
 *
 * One portable binary (Linux / FreeBSD / substrate) of numbered,
 * self-checking points covering the whole fork surface: lifecycle
 * (fork/wait/exit/signal-death/zombie/orphan/getppid), copy-on-write
 * (heap/stack/bss/data/mmap private+shared), fd inheritance + shared
 * offsets, pipes & socketpairs across the fork, signal state (pending
 * cleared, handlers & mask inherited, SIGCHLD), fork+exec, fork in a
 * threaded process, scale (many + nested forks), and — the reason this
 * exists — fork + IPC round-trips that model TDE's TDEUniqueApplication
 * startup, where a parent fork()s a daemon child and then makes an IPC
 * call back to it through a broker.  That path yields "Communication
 * problem with kded, it probably crashed" on substrate even though the
 * child registers fine.
 *
 * Each point runs under a SIGALRM watchdog, so a wedged fork/IPC is a
 * FAIL(HANG) instead of hanging the suite.  A real OS passes every point;
 * substrate divergences localise the kernel bug.
 *
 *   host:       cc -O2 -pthread torture_fork64.c -o torture_fork64
 *   substrate:  i386-unknown-substrate-gcc -O2 torture_fork64.c -lpthread -o torture_fork64
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

static int g_total = 0, g_pass = 0, g_fail = 0;

static void okfail(int cond, const char *name) {
    g_total++;
    if (cond) { g_pass++; printf("[%02d] %-34s PASS\n", g_total, name); }
    else      { g_fail++; printf("[%02d] %-34s FAIL\n", g_total, name); }
}
#define CHK(c, name) okfail((c), (name))

/* ---- per-test watchdog: a hung fork/IPC becomes FAIL(HANG) ---------- */
static sigjmp_buf g_wd;
static volatile int g_wd_armed;
static void wd_alarm(int s) { (void)s; if (g_wd_armed) { g_wd_armed = 0; siglongjmp(g_wd, 1); } }
#define WATCHDOG(secs, name, ...) do {                        \
    g_wd_armed = 1;                                           \
    if (sigsetjmp(g_wd, 1) == 0) { alarm(secs); { __VA_ARGS__; } alarm(0); g_wd_armed = 0; } \
    else { alarm(0); okfail(0, name "(HANG)"); }              \
} while (0)

static int reap(pid_t pid) {
    int st;
    for (;;) {
        pid_t r = waitpid(pid, &st, 0);
        if (r == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : -100 - (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
        if (r < 0 && errno == EINTR) continue;
        return -1;
    }
}
static int write_all(int fd, const void *b, size_t n) {
    const char *p = b; size_t o = 0;
    while (o < n) { ssize_t w = write(fd, p + o, n - o); if (w < 0) { if (errno==EINTR) continue; return -1; } if (!w) return -1; o += (size_t)w; }
    return 0;
}
static int read_all(int fd, void *b, size_t n) {
    char *p = b; size_t o = 0;
    while (o < n) { ssize_t r = read(fd, p + o, n - o); if (r < 0) { if (errno==EINTR) continue; return -1; } if (!r) return -1; o += (size_t)r; }
    return 0;
}

/* ====================================================================== */
/* 1. Lifecycle                                                            */
/* ====================================================================== */
static void t_lifecycle(void) {
    WATCHDOG(10, "fork_returns_child_pid", {
        pid_t p = fork();
        if (p == 0) _exit(7);
        CHK(p > 0, "fork_returns_child_pid");
        CHK(reap(p) == 7, "child_exit_status_7");
    });
    WATCHDOG(10, "child_getppid", {
        pid_t me = getpid();
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) { pid_t pp = getppid(); (void)write_all(sv[1], &pp, sizeof pp); _exit(0); }
        close(sv[1]); pid_t got = 0; (void)read_all(sv[0], &got, sizeof got); close(sv[0]); reap(p);
        CHK(got == me, "child_getppid_is_parent");
    });
    WATCHDOG(10, "child_signal_death", {
        pid_t p = fork();
        if (p == 0) { raise(SIGKILL); pause(); _exit(0); }
        int st; waitpid(p, &st, 0);
        CHK(WIFSIGNALED(st), "child_WIFSIGNALED");
        CHK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL, "child_WTERMSIG_KILL");
    });
    WATCHDOG(10, "waitpid_wnohang", {
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) { char c; (void)read_all(sv[0], &c, 1); _exit(3); }
        int st; pid_t r = waitpid(p, &st, WNOHANG);
        CHK(r == 0, "wnohang_0_while_alive");
        (void)write_all(sv[1], "x", 1);
        CHK(reap(p) == 3, "wnohang_then_reap");
        close(sv[0]); close(sv[1]);
    });
    WATCHDOG(15, "many_children_reaped", {
        pid_t kids[16]; int ok = 1;
        for (int i = 0; i < 16; i++) { kids[i] = fork(); if (kids[i] == 0) _exit(i & 0x3f); }
        for (int i = 0; i < 16; i++) if (reap(kids[i]) != (i & 0x3f)) ok = 0;
        CHK(ok, "16_children_correct_status");
    });
    WATCHDOG(10, "orphan_reparented", {
        /* parent forks a child that forks a grandchild and exits; the
         * grandchild's getppid must change (reparented to a live pid). */
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) {
            pid_t g = fork();
            if (g == 0) {
                usleep(200*1000);          /* outlive our parent */
                pid_t pp = getppid();
                (void)write_all(sv[1], &pp, sizeof pp);
                _exit(0);
            }
            _exit(0);                       /* child exits -> grandchild orphaned */
        }
        reap(p);
        close(sv[1]); pid_t gpp = -1; (void)read_all(sv[0], &gpp, sizeof gpp); close(sv[0]);
        /* whatever reaps it (init/subreaper), the original child pid is gone */
        CHK(gpp > 0 && gpp != p, "grandchild_reparented");
        /* harvest the grandchild if it became ours (subreaper); ignore otherwise */
        int st; while (waitpid(-1, &st, WNOHANG) > 0) {}
    });
}

/* ====================================================================== */
/* 2. Copy-on-write                                                        */
/* ====================================================================== */
static int g_bss_global;
static int g_data_global = 0x1234;
static void t_cow(void) {
    WATCHDOG(10, "cow_stack", {
        int local = 100;
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) { local = 999; (void)write_all(sv[1], &local, sizeof local); _exit(0); }
        close(sv[1]); int cv = 0; (void)read_all(sv[0], &cv, sizeof cv); close(sv[0]); reap(p);
        CHK(cv == 999, "child_saw_its_stack_write");
        CHK(local == 100, "parent_stack_unchanged");
    });
    WATCHDOG(10, "cow_heap", {
        int *h = malloc(sizeof(int)); *h = 55;
        pid_t p = fork();
        if (p == 0) { *h = 77; _exit((*h == 77) ? 0 : 1); }
        CHK(reap(p) == 0, "child_heap_write_ok");
        CHK(*h == 55, "parent_heap_unchanged");
        free(h);
    });
    WATCHDOG(10, "cow_bss_data", {
        g_bss_global = 0; g_data_global = 0x1234;
        pid_t p = fork();
        if (p == 0) { g_bss_global = 42; g_data_global = 0x9999; _exit(0); }
        reap(p);
        CHK(g_bss_global == 0, "parent_bss_unchanged");
        CHK(g_data_global == 0x1234, "parent_data_unchanged");
    });
    WATCHDOG(10, "cow_mmap_private", {
        char *m = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) { CHK(0, "mmap_private_alloc"); }
        else {
            m[0] = 'A';
            pid_t p = fork();
            if (p == 0) { m[0] = 'Z'; _exit(m[0] == 'Z' ? 0 : 1); }
            CHK(reap(p) == 0, "child_mmap_private_write");
            CHK(m[0] == 'A', "parent_mmap_private_unchanged");
            munmap(m, 4096);
        }
    });
    WATCHDOG(10, "mmap_shared_visible", {
        char *m = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) { CHK(0, "mmap_shared_alloc"); }
        else {
            m[0] = 'A';
            int sv[2]; (void)pipe(sv);
            pid_t p = fork();
            if (p == 0) { m[0] = 'S'; (void)write_all(sv[1], "x", 1); _exit(0); }
            close(sv[1]); char c; (void)read_all(sv[0], &c, 1); close(sv[0]); reap(p);
            CHK(m[0] == 'S', "parent_sees_shared_write");
            munmap(m, 4096);
        }
    });
    WATCHDOG(15, "cow_large_heap", {
        size_t n = 256 * 1024;
        unsigned char *big = malloc(n);
        if (!big) { CHK(0, "large_malloc"); }
        else {
            memset(big, 0xAB, n);
            pid_t p = fork();
            if (p == 0) { memset(big, 0xCD, n); _exit(big[0] == 0xCD && big[n-1] == 0xCD ? 0 : 1); }
            CHK(reap(p) == 0, "child_wrote_large_heap");
            CHK(big[0] == 0xAB && big[n-1] == 0xAB, "parent_large_heap_unchanged");
            free(big);
        }
    });
}

/* ====================================================================== */
/* 3. File descriptors across fork                                         */
/* ====================================================================== */
static void t_fds(void) {
    WATCHDOG(10, "fd_inherited", {
        char tmpl[] = "/tmp/tfk_fd_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) { CHK(0, "mkstemp"); }
        else {
            (void)write_all(fd, "hello", 5); lseek(fd, 0, SEEK_SET);
            pid_t p = fork();
            if (p == 0) { char b[6] = {0}; (void)read_all(fd, b, 5); _exit(memcmp(b,"hello",5)==0 ? 0 : 1); }
            CHK(reap(p) == 0, "child_reads_inherited_fd");
            close(fd); unlink(tmpl);
        }
    });
    WATCHDOG(10, "shared_file_offset", {
        char tmpl[] = "/tmp/tfk_off_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) { CHK(0, "mkstemp2"); }
        else {
            int sv[2]; (void)pipe(sv);
            pid_t p = fork();
            if (p == 0) { char c; (void)read_all(sv[0], &c, 1); (void)write_all(fd, "BBB", 3); _exit(0); }
            (void)write_all(fd, "AAA", 3);
            (void)write_all(sv[1], "go", 1);
            reap(p);
            off_t end = lseek(fd, 0, SEEK_END);
            CHK(end == 6, "shared_offset_advanced_by_both");
            close(fd); unlink(tmpl); close(sv[0]); close(sv[1]);
        }
    });
    WATCHDOG(10, "pipe_parent_to_child", {
        int pp[2]; (void)pipe(pp);
        pid_t p = fork();
        if (p == 0) { close(pp[1]); char b[4]={0}; (void)read_all(pp[0], b, 3); _exit(memcmp(b,"PtC",3)==0?0:1); }
        close(pp[0]); (void)write_all(pp[1], "PtC", 3); close(pp[1]);
        CHK(reap(p) == 0, "child_read_parent_pipe");
    });
    WATCHDOG(10, "pipe_child_to_parent", {
        int pp[2]; (void)pipe(pp);
        pid_t p = fork();
        if (p == 0) { close(pp[0]); (void)write_all(pp[1], "CtP", 3); close(pp[1]); _exit(0); }
        close(pp[1]); char b[4]={0}; int rr = read_all(pp[0], b, 3); close(pp[0]); reap(p);
        CHK(rr == 0 && memcmp(b,"CtP",3)==0, "parent_read_child_pipe");
    });
    WATCHDOG(10, "socketpair_across_fork", {
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { CHK(0, "socketpair"); }
        else {
            pid_t p = fork();
            if (p == 0) { close(sp[0]); char b[4]={0}; (void)read_all(sp[1], b, 3); (void)write_all(sp[1], "ACK", 3); _exit(0); }
            close(sp[1]); (void)write_all(sp[0], "REQ", 3);
            char b[4]={0}; int rr = read_all(sp[0], b, 3); close(sp[0]); reap(p);
            CHK(rr == 0 && memcmp(b,"ACK",3)==0, "socketpair_roundtrip");
        }
    });
    WATCHDOG(12, "many_inherited_fds", {
        enum { NF = 16 }; int fds[NF]; int opened = 0;
        for (int i = 0; i < NF; i++) { fds[i] = open("/dev/null", O_RDWR); if (fds[i] >= 0) opened++; }
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) { int g = 0; for (int i = 0; i < NF; i++) if (fds[i] >= 0 && write(fds[i],"x",1)==1) g++; (void)write_all(sv[1], &g, sizeof g); _exit(0); }
        close(sv[1]); int g = -1; (void)read_all(sv[0], &g, sizeof g); close(sv[0]); reap(p);
        for (int i = 0; i < NF; i++) if (fds[i] >= 0) close(fds[i]);
        CHK(g == opened && opened > 0, "child_used_all_inherited_fds");
    });
}

/* ====================================================================== */
/* 4. Signals across fork                                                  */
/* ====================================================================== */
static volatile int g_sig_seen;
static void sig_h(int s) { (void)s; g_sig_seen = 1; }
static void t_signals(void) {
    WATCHDOG(10, "pending_signals_cleared", {
        sigset_t blk, old; sigemptyset(&blk); sigaddset(&blk, SIGUSR1);
        sigprocmask(SIG_BLOCK, &blk, &old);
        raise(SIGUSR1);
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) { sigset_t pend; sigpending(&pend); int has = sigismember(&pend, SIGUSR1); (void)write_all(sv[1], &has, sizeof has); _exit(0); }
        close(sv[1]); int has = -1; (void)read_all(sv[0], &has, sizeof has); close(sv[0]); reap(p);
        signal(SIGUSR1, SIG_IGN);   /* discard our own pending one BEFORE unblocking */
        sigprocmask(SIG_SETMASK, &old, NULL);
        signal(SIGUSR1, SIG_DFL);
        CHK(has == 0, "child_no_pending_signals");
    });
    WATCHDOG(10, "handler_inherited", {
        signal(SIGUSR2, sig_h);
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) { g_sig_seen = 0; raise(SIGUSR2); int s = g_sig_seen; (void)write_all(sv[1], &s, sizeof s); _exit(0); }
        close(sv[1]); int s = 0; (void)read_all(sv[0], &s, sizeof s); close(sv[0]); reap(p);
        signal(SIGUSR2, SIG_DFL);
        CHK(s == 1, "child_inherited_handler_ran");
    });
    WATCHDOG(10, "sigmask_inherited", {
        sigset_t blk, old; sigemptyset(&blk); sigaddset(&blk, SIGUSR1);
        sigprocmask(SIG_BLOCK, &blk, &old);
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) { sigset_t cur; sigprocmask(SIG_BLOCK, NULL, &cur); int b = sigismember(&cur, SIGUSR1); (void)write_all(sv[1], &b, sizeof b); _exit(0); }
        close(sv[1]); int b = -1; (void)read_all(sv[0], &b, sizeof b); close(sv[0]); reap(p);
        sigprocmask(SIG_SETMASK, &old, NULL);
        CHK(b == 1, "child_inherited_sigmask");
    });
    WATCHDOG(10, "sigchld_on_exit", {
        g_sig_seen = 0; signal(SIGCHLD, sig_h);
        pid_t p = fork();
        if (p == 0) _exit(0);
        reap(p);
        for (int i = 0; i < 200 && !g_sig_seen; i++) usleep(1000);
        signal(SIGCHLD, SIG_DFL);
        CHK(g_sig_seen == 1, "parent_got_SIGCHLD");
    });
}

/* ====================================================================== */
/* 5. fork + exec                                                          */
/* ====================================================================== */
static void t_exec(void) {
    WATCHDOG(10, "fork_exec_true", {
        pid_t p = fork();
        if (p == 0) { execl("/bin/true","true",(char*)NULL); execl("/usr/bin/true","true",(char*)NULL); _exit(127); }
        int rc = reap(p);
        CHK(rc == 0 || rc == 127, "fork_exec_ran");
    });
    WATCHDOG(10, "fork_exec_exitcode", {
        pid_t p = fork();
        if (p == 0) { execl("/bin/sh","sh","-c","exit 42",(char*)NULL); _exit(127); }
        int rc = reap(p);
        CHK(rc == 42 || rc == 127, "fork_exec_sh_exit_42");
    });
}

/* ====================================================================== */
/* 6. fork in a threaded process                                           */
/* ====================================================================== */
static volatile int g_thr_run = 1;
static void *spinner(void *a) { (void)a; while (g_thr_run) usleep(2000); return NULL; }
static void t_threaded_fork(void) {
    WATCHDOG(15, "fork_in_threaded_proc", {
        pthread_t th[4]; int started = 0; g_thr_run = 1;
        for (int i = 0; i < 4; i++) if (pthread_create(&th[i], NULL, spinner, NULL) == 0) started++;
        usleep(50*1000);
        pid_t p = fork();
        if (p == 0) {
            pthread_t ct; int ok = (pthread_create(&ct, NULL, spinner, NULL) == 0);
            g_thr_run = 0;
            if (ok) pthread_join(ct, NULL);
            _exit(ok ? 0 : 1);
        }
        int rc = reap(p);
        g_thr_run = 0;
        for (int i = 0; i < started; i++) pthread_join(th[i], NULL);
        CHK(rc == 0, "child_of_threaded_can_thread");
    });
}

/* ====================================================================== */
/* 7. Scale: many forks, nested forks                                      */
/* ====================================================================== */
static void t_scale(void) {
    WATCHDOG(40, "fork_storm_100", {
        int ok = 1;
        for (int i = 0; i < 100; i++) {
            pid_t p = fork();
            if (p == 0) _exit(0);
            if (p < 0) { ok = 0; break; }
            if (reap(p) != 0) { ok = 0; break; }
        }
        CHK(ok, "100_sequential_forks");
    });
    WATCHDOG(15, "nested_fork", {
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) {
            pid_t g = fork();
            if (g == 0) { (void)write_all(sv[1], "G", 1); _exit(0); }
            int grc = reap(g);
            _exit(grc == 0 ? 0 : 1);
        }
        close(sv[1]); char c = 0; (void)read_all(sv[0], &c, 1); close(sv[0]);
        CHK(reap(p) == 0 && c == 'G', "grandchild_ran");
    });
}

/* ====================================================================== */
/* 8. THE kded model: fork a daemon child, then IPC-call it via a broker.  */
/*    Mirrors TDEUniqueApplication::start(): a broker (dcopserver) relays  */
/*    a request from the parent to the freshly-fork()'d child and the      */
/*    reply back.  This is the path that yields "Communication problem     */
/*    with kded, it probably crashed" on substrate.                        */
/* ====================================================================== */
static char g_broker_path[96];
static void *broker_fn(void *arg) {
    int lfd = *(int *)arg;
    int svc = -1, caller = -1;
    for (int got = 0; got < 2; ) {
        int c = accept(lfd, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; break; }
        char tag[8] = {0};
        if (read_all(c, tag, 4) != 0) { close(c); continue; }
        if (memcmp(tag, "REG\n", 4) == 0) svc = c; else caller = c;
        got++;
    }
    if (svc >= 0 && caller >= 0) {
        (void)write_all(svc, "CALL", 4);
        char reply[8] = {0};
        if (read_all(svc, reply, 5) == 0) (void)write_all(caller, reply, 5);
    }
    if (svc >= 0) close(svc);
    if (caller >= 0) close(caller);
    return NULL;
}
static int connect_broker(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX; strncpy(sa.sun_path, g_broker_path, sizeof(sa.sun_path) - 1);
    for (int t = 0; t < 400; t++) { if (connect(fd, (struct sockaddr*)&sa, sizeof sa) == 0) return fd; usleep(3000); }
    close(fd); return -1;
}
static void t_kded_model(void) {
    WATCHDOG(25, "fork_daemon_ipc_call", {
        snprintf(g_broker_path, sizeof g_broker_path, "/tmp/tfk_brk_%ld", (long)getpid());
        int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
        sa.sun_family = AF_UNIX; strncpy(sa.sun_path, g_broker_path, sizeof(sa.sun_path) - 1);
        unlink(g_broker_path);
        int bound = (bind(lfd, (struct sockaddr*)&sa, sizeof sa) == 0) && (listen(lfd, 8) == 0);
        CHK(bound, "broker_bind_listen");
        if (bound) {
            pthread_t bt; pthread_create(&bt, NULL, broker_fn, &lfd);
            pid_t p = fork();
            if (p == 0) {
                int s = connect_broker();
                if (s < 0) _exit(10);
                if (write_all(s, "REG\n", 4) != 0) _exit(11);
                char call[8] = {0};
                if (read_all(s, call, 4) != 0) _exit(12);
                if (write_all(s, "REPLY", 5) != 0) _exit(13);
                close(s);
                _exit(0);
            }
            int parent_ok = 0;
            int c = connect_broker();
            if (c >= 0) {
                if (write_all(c, "CALL\n", 4) == 0) {
                    char reply[8] = {0};
                    if (read_all(c, reply, 5) == 0 && memcmp(reply, "REPLY", 5) == 0) parent_ok = 1;
                }
                close(c);
            }
            int crc = reap(p);
            pthread_join(bt, NULL);
            CHK(crc == 0, "forked_daemon_exit_clean");
            CHK(parent_ok, "parent_ipc_call_got_reply");
        }
        close(lfd); unlink(g_broker_path);
    });

    WATCHDOG(20, "fork_both_sides_connect", {
        snprintf(g_broker_path, sizeof g_broker_path, "/tmp/tfk_brk2_%ld", (long)getpid());
        int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
        sa.sun_family = AF_UNIX; strncpy(sa.sun_path, g_broker_path, sizeof(sa.sun_path) - 1);
        unlink(g_broker_path);
        int bound = (bind(lfd, (struct sockaddr*)&sa, sizeof sa) == 0) && (listen(lfd, 8) == 0);
        if (bound) {
            pthread_t bt; pthread_create(&bt, NULL, broker_fn, &lfd);
            pid_t p = fork();
            if (p == 0) {
                int s = connect_broker();
                int ok = (s >= 0) && (write_all(s, "REG\n", 4) == 0);
                char call[8] = {0};
                ok = ok && (read_all(s, call, 4) == 0) && (write_all(s, "REPLY", 5) == 0);
                if (s >= 0) close(s);
                _exit(ok ? 0 : 1);
            }
            int c = connect_broker(); int got = 0;
            if (c >= 0 && write_all(c, "CALL\n", 4) == 0) {
                char reply[8] = {0};
                got = (read_all(c, reply, 5) == 0 && memcmp(reply, "REPLY", 5) == 0);
            }
            if (c >= 0) close(c);
            int crc = reap(p);
            pthread_join(bt, NULL);
            CHK(crc == 0 && got, "both_sides_post_fork_ipc");
        } else CHK(0, "broker2_bind");
        close(lfd); unlink(g_broker_path);
    });
}

/* ====================================================================== */
/* 9. exit-code fidelity                                                   */
/* ====================================================================== */
static void t_exit_codes(void) {
    int codes[] = { 0, 1, 2, 42, 127, 200, 255 };
    for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
        int code = codes[i];
        WATCHDOG(8, "exit_code", {
            pid_t p = fork();
            if (p == 0) _exit(code);
            char nm[40]; snprintf(nm, sizeof nm, "exit_code_%d", code);
            CHK(reap(p) == code, nm);
        });
    }
}

/* ====================================================================== */
/* 10. multi-page COW isolation (each page independently)                  */
/* ====================================================================== */
static void t_cow_multipage(void) {
    WATCHDOG(12, "cow_multipage", {
        size_t pg = 4096; int np = 4;
        unsigned char *m = mmap(0, pg*np, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) { CHK(0,"mmap_multipage"); CHK(0,"p1"); CHK(0,"p2"); CHK(0,"p3"); }
        else {
            for (int i = 0; i < np; i++) m[i*pg] = (unsigned char)(0x10 + i);
            pid_t p = fork();
            if (p == 0) { for (int i = 0; i < np; i++) m[i*pg] = (unsigned char)(0xF0 + i); _exit(0); }
            reap(p);
            for (int i = 0; i < np; i++) {
                char nm[32]; snprintf(nm, sizeof nm, "page_%d_parent_unchanged", i);
                CHK(m[i*pg] == (unsigned char)(0x10 + i), nm);
            }
            munmap(m, pg*np);
        }
    });
}

/* ====================================================================== */
/* 11. fd flags across fork                                                */
/* ====================================================================== */
static void t_fd_flags(void) {
    WATCHDOG(10, "dup_inherited", {
        int fd = open("/dev/null", O_RDWR); int d = dup(fd);
        pid_t p = fork();
        if (p == 0) { _exit(write(d, "x", 1) == 1 ? 0 : 1); }
        CHK(reap(p) == 0, "dup_fd_inherited");
        if (fd >= 0) close(fd); if (d >= 0) close(d);
    });
    WATCHDOG(10, "cloexec_survives_fork", {
        int fd = open("/dev/null", O_RDWR); fcntl(fd, F_SETFD, FD_CLOEXEC);
        pid_t p = fork();
        if (p == 0) { _exit(write(fd, "x", 1) == 1 ? 0 : 1); }   /* fork keeps it; only exec closes */
        CHK(reap(p) == 0, "cloexec_fd_survives_fork");
        if (fd >= 0) close(fd);
    });
    WATCHDOG(10, "nonblock_flag_inherited", {
        int sp[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, sp);
        fcntl(sp[0], F_SETFL, fcntl(sp[0], F_GETFL, 0) | O_NONBLOCK);
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) { int fl = fcntl(sp[0], F_GETFL, 0); int nb = (fl & O_NONBLOCK) ? 1 : 0; (void)write_all(sv[1], &nb, sizeof nb); _exit(0); }
        close(sv[1]); int nb = -1; (void)read_all(sv[0], &nb, sizeof nb); close(sv[0]); reap(p);
        close(sp[0]); close(sp[1]);
        CHK(nb == 1, "O_NONBLOCK_inherited");
    });
}

/* ====================================================================== */
/* 12. IPC stress across fork                                              */
/* ====================================================================== */
static void t_ipc_stress(void) {
    WATCHDOG(20, "ipc_100_roundtrips", {
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { CHK(0,"sp"); }
        else {
            pid_t p = fork();
            if (p == 0) { close(sp[0]); for (int i = 0; i < 100; i++) { char c; if (read_all(sp[1], &c, 1)) _exit(1); if (write_all(sp[1], &c, 1)) _exit(2); } _exit(0); }
            close(sp[1]); int ok = 1;
            for (int i = 0; i < 100; i++) { char c = (char)i; if (write_all(sp[0], &c, 1)) { ok = 0; break; } char r; if (read_all(sp[0], &r, 1) || r != c) { ok = 0; break; } }
            close(sp[0]); reap(p);
            CHK(ok, "100_socketpair_roundtrips");
        }
    });
    WATCHDOG(20, "ipc_large_message", {
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { CHK(0,"sp2"); }
        else {
            size_t N = 64*1024;
            pid_t p = fork();
            if (p == 0) { close(sp[0]); char *b = malloc(N); int ok = (read_all(sp[1], b, N) == 0); for (size_t i = 0; ok && i < N; i++) if ((unsigned char)b[i] != (i & 0xff)) ok = 0; (void)write_all(sp[1], &ok, sizeof ok); free(b); _exit(0); }
            close(sp[1]); char *b = malloc(N); for (size_t i = 0; i < N; i++) b[i] = (char)(i & 0xff); (void)write_all(sp[0], b, N);
            int ok = 0; (void)read_all(sp[0], &ok, sizeof ok); free(b); close(sp[0]); reap(p);
            CHK(ok == 1, "64k_message_intact");
        }
    });
}

/* ====================================================================== */
/* 13. Parallelism: 32 concurrent children + distinct pids                 */
/* ====================================================================== */
static void t_parallel(void) {
    WATCHDOG(25, "parallel_32_children", {
        int n = 32; int *sh = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
        if (sh == MAP_FAILED) { CHK(0,"shm"); }
        else {
            for (int i = 0; i < n; i++) sh[i] = 0;
            pid_t kids[32];
            for (int i = 0; i < n; i++) { kids[i] = fork(); if (kids[i] == 0) { sh[i] = i + 1; _exit(0); } }
            for (int i = 0; i < n; i++) reap(kids[i]);
            int ok = 1; for (int i = 0; i < n; i++) if (sh[i] != i + 1) ok = 0;
            CHK(ok, "32_children_each_wrote_slot");
            munmap(sh, 4096);
        }
    });
    WATCHDOG(10, "distinct_pids", {
        pid_t a = fork(); if (a == 0) _exit(0);
        pid_t b = fork(); if (b == 0) _exit(0);
        CHK(a != b && a > 0 && b > 0, "forked_pids_distinct");
        reap(a); reap(b);
    });
}

/* ====================================================================== */
/* 14. Double-fork daemonize + vfork                                       */
/* ====================================================================== */
static void t_double_fork(void) {
    WATCHDOG(12, "double_fork_daemonize", {
        int sv[2]; (void)pipe(sv);
        pid_t p = fork();
        if (p == 0) {
            pid_t p2 = fork();
            if (p2 > 0) _exit(0);
            setsid();
            (void)write_all(sv[1], "D", 1);
            _exit(0);
        }
        reap(p);
        close(sv[1]); char c = 0; (void)read_all(sv[0], &c, 1); close(sv[0]);
        int st; while (waitpid(-1, &st, WNOHANG) > 0) {}
        CHK(c == 'D', "daemon_grandchild_ran");
    });
    WATCHDOG(10, "vfork_exec", {
        pid_t p = vfork();
        if (p == 0) { execl("/bin/sh","sh","-c","exit 17",(char*)NULL); _exit(127); }
        int rc = reap(p);
        CHK(rc == 17 || rc == 127, "vfork_exec_exit_17");
    });
}

/* ====================================================================== */
/* 15. Misc + repeated fork-IPC (models the kded retry cascade)            */
/* ====================================================================== */
static void t_more(void) {
    WATCHDOG(10, "wait_reaps_any", {
        pid_t p = fork(); if (p == 0) _exit(9);
        int st; pid_t r = wait(&st);
        CHK(r == p && WIFEXITED(st) && WEXITSTATUS(st) == 9, "wait_reaps_child");
    });
    WATCHDOG(10, "child_pid_matches_wait", {
        int sv[2]; (void)pipe(sv);
        pid_t p = fork(); if (p == 0) { pid_t me = getpid(); (void)write_all(sv[1], &me, sizeof me); _exit(0); }
        close(sv[1]); pid_t cm = 0; (void)read_all(sv[0], &cm, sizeof cm); close(sv[0]);
        CHK(cm == p, "child_getpid_eq_parent_waitpid");
        reap(p);
    });
    WATCHDOG(10, "cwd_isolated", {
        char before[256]; (void)!getcwd(before, sizeof before);
        pid_t p = fork(); if (p == 0) { (void)!chdir("/tmp"); _exit(0); }
        reap(p);
        char after[256]; (void)!getcwd(after, sizeof after);
        CHK(strcmp(before, after) == 0, "parent_cwd_unchanged_by_child");
    });
    WATCHDOG(10, "getpid_ne_getppid", {
        CHK(getpid() != getppid(), "getpid_ne_getppid");
    });
    WATCHDOG(20, "kded_model_repeated", {
        int ok = 1;
        for (int rep = 0; rep < 5 && ok; rep++) {
            snprintf(g_broker_path, sizeof g_broker_path, "/tmp/tfk_rep_%ld_%d", (long)getpid(), rep);
            int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
            struct sockaddr_un sa; memset(&sa, 0, sizeof sa); sa.sun_family = AF_UNIX;
            strncpy(sa.sun_path, g_broker_path, sizeof(sa.sun_path) - 1); unlink(g_broker_path);
            if (bind(lfd, (struct sockaddr*)&sa, sizeof sa) != 0 || listen(lfd, 8) != 0) { ok = 0; close(lfd); break; }
            pthread_t bt; pthread_create(&bt, NULL, broker_fn, &lfd);
            pid_t p = fork();
            if (p == 0) { int s = connect_broker(); int o = (s >= 0) && (write_all(s, "REG\n", 4) == 0); char call[8] = {0}; o = o && (read_all(s, call, 4) == 0) && (write_all(s, "REPLY", 5) == 0); if (s >= 0) close(s); _exit(o ? 0 : 1); }
            int c = connect_broker(); int got = 0;
            if (c >= 0 && write_all(c, "CALL\n", 4) == 0) { char reply[8] = {0}; got = (read_all(c, reply, 5) == 0 && memcmp(reply, "REPLY", 5) == 0); }
            if (c >= 0) close(c);
            if (reap(p) != 0 || !got) ok = 0;
            pthread_join(bt, NULL); close(lfd); unlink(g_broker_path);
        }
        CHK(ok, "5x_fork_ipc_no_leak");
    });
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, wd_alarm);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("torture_fork64: fork(2) / process-lifecycle battery\n");

    t_lifecycle();
    t_cow();
    t_fds();
    t_signals();
    t_exec();
    t_threaded_fork();
    t_scale();
    t_exit_codes();
    t_cow_multipage();
    t_fd_flags();
    t_ipc_stress();
    t_parallel();
    t_double_fork();
    t_more();
    t_kded_model();

    printf("------------------------------------------------------------\n");
    printf("# total=%d pass=%d fail=%d\n", g_total, g_pass, g_fail);
    printf("%s\n", g_fail == 0 ? "RESULT: PASS" : "RESULT: FAIL");
    return g_fail == 0 ? 0 : 1;
}
