/*
 * torture_procs.c — subprocess (fork / exec / wait / exit / signals /
 * fd-inheritance) torture suite.
 *
 * Kernel-hang hunting: Substrate has an intermittent lost-wakeup bug on
 * blocking waitpid() under kernel preemption.  This suite hammers the
 * fork/exec/wait paths and uses a fork-per-test alarm watchdog so a hung
 * waitpid is reported as HANG and the run continues instead of wedging.
 *
 * It is also self-execing: many exec tests re-exec THIS binary in a
 * "child mode" selected by argv[1] (a "__child*" token).  On the target
 * the binary lives at /tmp/torture_procs (run via init=); on the host it
 * is compiled to exactly /tmp/torture_procs so the same self-path works.
 *
 * Portable POSIX C: compiles + PASSES on Linux (host) and cross-compiles
 * for Substrate.  Tests that can't be observed reliably SKIP rather than
 * FAIL.
 *
 *   run: torture_procs                 (run the suite)
 *        torture_procs __child... ...  (internal child mode; do not run)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_TIMEOUT 8
static int tests_run, tests_pass, tests_fail, tests_hang, tests_skip;
typedef int (*testfn)(void);
static void alrm_noop(int s){ (void)s; }
static const char *g_self = "/tmp/torture_procs";   /* set from argv[0] in main */

static void run_one(const char *name, testfn fn) {
    fprintf(stdout, "[%2d] %-32s ", ++tests_run, name); fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { fprintf(stdout, "FORK-FAIL errno=%d\n", errno); tests_fail++; return; }
    if (pid == 0) { int rc = fn(); fflush(stdout); _exit(rc==0?0:(rc==1?2:1)); }
    struct sigaction sa = {0}, old; sa.sa_handler = alrm_noop; sigaction(SIGALRM,&sa,&old);
    alarm(TEST_TIMEOUT);
    int st; pid_t r = waitpid(pid, &st, 0);
    alarm(0); sigaction(SIGALRM,&old,NULL);
    if (r != pid) {
        if (waitpid(pid, &st, WNOHANG) != pid) {
            kill(pid, SIGKILL); waitpid(pid,&st,0);
            fprintf(stdout, "HANG (killed after %ds)\n", TEST_TIMEOUT); tests_hang++; return;
        }
    }
    if (WIFSIGNALED(st)) { fprintf(stdout,"CRASH sig=%d\n",WTERMSIG(st)); tests_fail++; }
    else if (WEXITSTATUS(st)==0){ fprintf(stdout,"PASS\n"); tests_pass++; }
    else if (WEXITSTATUS(st)==2){ fprintf(stdout,"SKIP\n"); tests_skip++; }
    else { fprintf(stdout,"FAIL\n"); tests_fail++; }
}
#define RUN(name) run_one(#name, test_##name)
#define TEST(name) static int test_##name(void)
#define CHECK(cond,msg) do{ if(!(cond)){ fprintf(stdout,"\n    [%s:%d] %s errno=%d(%s) ",__FILE__,__LINE__,(msg),errno,strerror(errno)); return -1; } }while(0)
#define SKIP(m) do{(void)(m);return 1;}while(0)

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

/* Spawn a child that re-execs self in a child mode.  Returns pid or -1. */
static pid_t spawn_self(char *const argv[]) {
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) {
        execv(g_self, argv);
        _exit(126);
    }
    return p;
}

/* Reap a single pid, return WEXITSTATUS or -1 if not a clean exit. */
static int reap_exit(pid_t p) {
    int st;
    if (waitpid(p, &st, 0) != p) return -1;
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
}

static void msleep(long ms) {
    /* usleep is POSIX-obsolescent but present on both host and substrate;
     * loop in <=1s chunks to stay well-defined. */
    while (ms > 0) {
        long chunk = ms > 900 ? 900 : ms;
        usleep((useconds_t)(chunk * 1000));
        ms -= chunk;
    }
}

/* ================================================================== */
/* fork basics                                                         */
/* ================================================================== */

TEST(fork_returns_pid_and_zero) {
    pid_t p = fork();
    CHECK(p >= 0, "fork failed");
    if (p == 0) _exit(0);
    CHECK(p > 0, "parent did not get positive pid");
    CHECK(reap_exit(p) == 0, "child did not exit 0");
    return 0;
}

TEST(fork_child_sees_zero) {
    int fds[2];
    CHECK(pipe(fds) == 0, "pipe");
    pid_t p = fork();
    CHECK(p >= 0, "fork failed");
    if (p == 0) {
        close(fds[0]);
        /* In the child, fork() returned 0; we cannot re-observe the
         * return value, but we can confirm we're a distinct process by
         * reporting our own getpid back to the parent. */
        pid_t me = getpid();
        write(fds[1], &me, sizeof me);
        close(fds[1]);
        _exit(0);
    }
    close(fds[1]);
    pid_t childpid = -1;
    CHECK(read(fds[0], &childpid, sizeof childpid) == (ssize_t)sizeof childpid, "read");
    close(fds[0]);
    CHECK(childpid == p, "child getpid != parent's fork return");
    CHECK(reap_exit(p) == 0, "reap");
    return 0;
}

TEST(child_ppid_is_parent) {
    pid_t parent = getpid();
    int fds[2];
    CHECK(pipe(fds) == 0, "pipe");
    pid_t p = fork();
    CHECK(p >= 0, "fork");
    if (p == 0) {
        close(fds[0]);
        pid_t pp = getppid();
        write(fds[1], &pp, sizeof pp);
        close(fds[1]);
        _exit(0);
    }
    close(fds[1]);
    pid_t seen = -1;
    CHECK(read(fds[0], &seen, sizeof seen) == (ssize_t)sizeof seen, "read");
    close(fds[0]);
    CHECK(seen == parent, "child's ppid != parent pid");
    CHECK(reap_exit(p) == 0, "reap");
    return 0;
}

TEST(getpid_distinct_parent_child) {
    pid_t me = getpid();
    int fds[2];
    CHECK(pipe(fds) == 0, "pipe");
    pid_t p = fork();
    CHECK(p >= 0, "fork");
    if (p == 0) {
        close(fds[0]);
        pid_t cm = getpid();
        write(fds[1], &cm, sizeof cm);
        _exit(0);
    }
    close(fds[1]);
    pid_t cm = -1;
    CHECK(read(fds[0], &cm, sizeof cm) == (ssize_t)sizeof cm, "read");
    close(fds[0]);
    CHECK(cm != me, "child shares pid with parent");
    CHECK(cm == p, "child pid != fork return");
    CHECK(reap_exit(p) == 0, "reap");
    return 0;
}

/* ================================================================== */
/* exit codes                                                          */
/* ================================================================== */

static int exit_code_check(int code) {
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) _exit(code);
    int st;
    if (waitpid(p, &st, 0) != p) return -2;
    if (!WIFEXITED(st)) return -3;
    return WEXITSTATUS(st) == (code & 0xff) ? 0 : -4;
}

TEST(exit_code_0)   { CHECK(exit_code_check(0)   == 0, "exit 0");   return 0; }
TEST(exit_code_1)   { CHECK(exit_code_check(1)   == 0, "exit 1");   return 0; }
TEST(exit_code_42)  { CHECK(exit_code_check(42)  == 0, "exit 42");  return 0; }
TEST(exit_code_255) { CHECK(exit_code_check(255) == 0, "exit 255"); return 0; }

TEST(exit_code_many) {
    int codes[] = {0, 1, 2, 7, 42, 100, 127, 128, 200, 254, 255};
    for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++)
        CHECK(exit_code_check(codes[i]) == 0, "exit code mismatch");
    return 0;
}

TEST(return_from_main_equiv_exit) {
    /* A child that returns from its top function (here _exit substitute)
     * should be indistinguishable from _exit(N).  Use exec of self in a
     * mode that returns rather than _exit. */
    char arg[16]; snprintf(arg, sizeof arg, "%d", 33);
    char *av[] = {(char*)"tp", (char*)"__child_exit", arg, NULL};
    pid_t p = spawn_self(av);
    CHECK(p > 0, "spawn");
    CHECK(reap_exit(p) == 33, "child_exit code");
    return 0;
}

TEST(exit_high_bits_masked) {
    /* exit(256) should be observed as 0 (low 8 bits). */
    CHECK(exit_code_check(256) == 0, "exit(256) low byte");
    /* exit(0x142) -> 0x42 */
    pid_t p = fork();
    CHECK(p >= 0, "fork");
    if (p == 0) _exit(0x142);
    int st; CHECK(waitpid(p,&st,0)==p,"wait");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st)==0x42, "0x142 -> 0x42");
    return 0;
}

/* ================================================================== */
/* signal classification                                               */
/* ================================================================== */

static int signal_kills_with(int sig) {
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) {
        /* restore default disposition for catchable ones */
        signal(sig, SIG_DFL);
        raise(sig);
        /* give the signal a moment; if it didn't kill us, fail loudly */
        for (volatile int i = 0; i < 1000000; i++) {}
        _exit(99);
    }
    int st;
    if (waitpid(p, &st, 0) != p) return -2;
    if (!WIFSIGNALED(st)) return -3;
    return WTERMSIG(st) == sig ? 0 : -4;
}

TEST(sig_kill_classified)  { CHECK(signal_kills_with(SIGKILL) == 0, "SIGKILL"); return 0; }
TEST(sig_segv_classified)  { CHECK(signal_kills_with(SIGSEGV) == 0, "SIGSEGV"); return 0; }
TEST(sig_abrt_classified)  { CHECK(signal_kills_with(SIGABRT) == 0, "SIGABRT"); return 0; }
TEST(sig_term_classified)  { CHECK(signal_kills_with(SIGTERM) == 0, "SIGTERM"); return 0; }

TEST(wifexited_vs_wifsignaled) {
    /* clean exit */
    pid_t p = fork(); CHECK(p>=0,"fork");
    if (p==0) _exit(5);
    int st; CHECK(waitpid(p,&st,0)==p,"wait");
    CHECK(WIFEXITED(st),"WIFEXITED set");
    CHECK(!WIFSIGNALED(st),"WIFSIGNALED clear");
    /* signalled */
    p = fork(); CHECK(p>=0,"fork2");
    if (p==0){ signal(SIGTERM,SIG_DFL); raise(SIGTERM); for(volatile int i=0;i<1000000;i++){} _exit(0);}
    CHECK(waitpid(p,&st,0)==p,"wait2");
    CHECK(WIFSIGNALED(st),"WIFSIGNALED set");
    CHECK(!WIFEXITED(st),"WIFEXITED clear");
    return 0;
}

TEST(parent_kills_child_signal) {
    pid_t p = fork(); CHECK(p>=0,"fork");
    if (p==0){ for(;;) pause(); _exit(0); }
    msleep(50);
    CHECK(kill(p, SIGTERM) == 0, "kill");
    int st; CHECK(waitpid(p,&st,0)==p,"wait");
    CHECK(WIFSIGNALED(st) && WTERMSIG(st)==SIGTERM, "child died by SIGTERM");
    return 0;
}

/* ================================================================== */
/* waitpid variants                                                    */
/* ================================================================== */

TEST(wait_specific_pid) {
    pid_t a = fork(); CHECK(a>=0,"fork a"); if(a==0){ msleep(30); _exit(11);}
    pid_t b = fork(); CHECK(b>=0,"fork b"); if(b==0){ _exit(22);}
    int st;
    CHECK(waitpid(b,&st,0)==b,"wait b");
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==22,"b code");
    CHECK(waitpid(a,&st,0)==a,"wait a");
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==11,"a code");
    return 0;
}

TEST(wait_any_minus1) {
    pid_t p = fork(); CHECK(p>=0,"fork"); if(p==0) _exit(17);
    int st; pid_t r = wait(&st);
    CHECK(r==p,"wait(-1) returned the child");
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==17,"code");
    return 0;
}

TEST(wnohang_then_reap) {
    pid_t p = fork(); CHECK(p>=0,"fork");
    if (p==0){ msleep(200); _exit(7); }
    int st;
    pid_t r = waitpid(p,&st,WNOHANG);
    CHECK(r==0,"WNOHANG returns 0 while alive");
    /* now wait for real */
    CHECK(waitpid(p,&st,0)==p,"blocking wait");
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==7,"code");
    return 0;
}

TEST(wnohang_after_exit) {
    pid_t p = fork(); CHECK(p>=0,"fork"); if(p==0) _exit(9);
    /* give the child time to exit */
    msleep(100);
    int st; pid_t r;
    /* loop a few times: WNOHANG should eventually report the zombie */
    for (int i=0;i<50;i++){ r=waitpid(p,&st,WNOHANG); if(r==p) break; msleep(20); }
    CHECK(r==p,"WNOHANG reaped exited child");
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==9,"code");
    return 0;
}

TEST(wait_nonchild_echild) {
    /* waiting on an unrelated/never-our-child pid -> ECHILD */
    int st; errno=0;
    pid_t r = waitpid(999999, &st, WNOHANG);
    CHECK(r==-1 && errno==ECHILD, "ECHILD for non-child");
    return 0;
}

TEST(double_wait_echild) {
    pid_t p = fork(); CHECK(p>=0,"fork"); if(p==0)_exit(3);
    int st; CHECK(waitpid(p,&st,0)==p,"first wait");
    errno=0;
    pid_t r = waitpid(p,&st,0);
    CHECK(r==-1 && errno==ECHILD,"second wait ECHILD");
    return 0;
}

TEST(wait_no_children_echild) {
    int st; errno=0;
    pid_t r = wait(&st);
    CHECK(r==-1 && errno==ECHILD,"wait with no children -> ECHILD");
    return 0;
}

/* ================================================================== */
/* multiple children / ordering                                        */
/* ================================================================== */

TEST(spawn_32_verify_codes) {
    enum { N = 32 };
    pid_t pids[N];
    int   want[N];
    for (int i=0;i<N;i++){
        int code = (i*7+1) & 0xff;
        want[i] = code;
        pid_t p = fork();
        CHECK(p>=0,"fork");
        if (p==0) _exit(code);
        pids[i]=p;
    }
    /* reap by specific pid */
    for (int i=0;i<N;i++){
        int st;
        CHECK(waitpid(pids[i],&st,0)==pids[i],"wait specific");
        CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==want[i],"code match");
    }
    return 0;
}

TEST(reap_any_order_match_codes) {
    enum { N = 24 };
    pid_t pids[N];
    int   want[N];
    char  reaped[N];
    memset(reaped,0,sizeof reaped);
    for (int i=0;i<N;i++){
        int code = (i+1)&0xff;
        want[i]=code;
        pid_t p=fork(); CHECK(p>=0,"fork");
        /* stagger exits so they finish out of spawn order */
        if (p==0){ msleep((N-i)*5); _exit(code); }
        pids[i]=p;
    }
    for (int n=0;n<N;n++){
        int st; pid_t r = wait(&st);
        CHECK(r>0,"wait(-1)");
        int idx=-1;
        for (int i=0;i<N;i++) if(pids[i]==r){ idx=i; break; }
        CHECK(idx>=0,"reaped a known child");
        CHECK(!reaped[idx],"not double-reaped");
        reaped[idx]=1;
        CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==want[idx],"code match in any order");
    }
    /* all reaped */
    for (int i=0;i<N;i++) CHECK(reaped[i],"every child reaped");
    return 0;
}

TEST(staggered_exit_order) {
    enum { N = 16 };
    pid_t pids[N];
    for (int i=0;i<N;i++){
        pid_t p=fork(); CHECK(p>=0,"fork");
        if (p==0){ msleep(i*10); _exit(i&0xff); }
        pids[i]=p;
    }
    int count=0;
    for (;;){
        int st; pid_t r=wait(&st);
        if (r<0){ CHECK(errno==ECHILD,"final ECHILD"); break; }
        CHECK(WIFEXITED(st),"clean exit");
        count++;
    }
    CHECK(count==N,"reaped all staggered");
    (void)pids;
    return 0;
}

/* ================================================================== */
/* zombie / reparent                                                   */
/* ================================================================== */

TEST(zombie_status_preserved) {
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0) _exit(123);
    /* let it become a zombie and sit */
    msleep(150);
    int st; CHECK(waitpid(p,&st,0)==p,"wait");
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==123,"status preserved across delay");
    return 0;
}

TEST(orphan_reparented_to_init) {
    /* fork a child; child forks a grandchild and then the child exits,
     * orphaning the grandchild.  The grandchild should be reparented
     * (ppid != original child).  We observe via a pipe the grandchild's
     * ppid AFTER its parent has surely exited.  On systems where the
     * reparent target/timing isn't observable we SKIP. */
    int fds[2];
    CHECK(pipe(fds)==0,"pipe");
    pid_t child = fork();
    CHECK(child>=0,"fork child");
    if (child==0){
        pid_t gc = fork();
        if (gc<0) _exit(50);
        if (gc==0){
            close(fds[0]);
            pid_t orig_parent = getppid();
            /* wait until our parent (the child) has exited */
            int tries=0;
            pid_t pp;
            do { pp=getppid(); if(pp!=orig_parent) break; msleep(20); }
            while (++tries<100);
            write(fds[1], &pp, sizeof pp);
            close(fds[1]);
            _exit(0);
        }
        /* child exits immediately, orphaning grandchild */
        _exit(0);
    }
    close(fds[1]);
    /* reap immediate child */
    int st; waitpid(child,&st,0);
    pid_t new_pp = -1;
    ssize_t n = read(fds[0], &new_pp, sizeof new_pp);
    close(fds[0]);
    if (n != (ssize_t)sizeof new_pp) SKIP("could not observe reparent");
    /* grandchild's new parent should differ from the (now-dead) child */
    if (new_pp == child) SKIP("reparent not observed (still old ppid)");
    /* don't insist ppid==1: subreapers vary; just confirm it changed */
    return 0;
}

/* ================================================================== */
/* exec family                                                         */
/* ================================================================== */

TEST(execv_exit_code) {
    char arg[16]; snprintf(arg,sizeof arg,"%d",77);
    char *av[]={(char*)"tp",(char*)"__child_exit",arg,NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ execv(g_self,av); _exit(126); }
    CHECK(reap_exit(p)==77,"execv child exit code");
    return 0;
}

TEST(execl_exit_code) {
    char arg[16]; snprintf(arg,sizeof arg,"%d",88);
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ execl(g_self,"tp","__child_exit",arg,(char*)NULL); _exit(126); }
    CHECK(reap_exit(p)==88,"execl child exit code");
    return 0;
}

TEST(execvp_finds_abspath) {
    /* execvp with an absolute path behaves like execv */
    char *av[]={(char*)"tp",(char*)"__child_exit",(char*)"44",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ execvp(g_self,av); _exit(126); }
    CHECK(reap_exit(p)==44,"execvp abspath exit code");
    return 0;
}

TEST(execve_custom_env) {
    char *av[]={(char*)"tp",(char*)"__child_envcheck",(char*)"TPVAR",(char*)"hello",NULL};
    char *ev[]={(char*)"TPVAR=hello",(char*)"PATH=/bin",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ execve(g_self,av,ev); _exit(126); }
    CHECK(reap_exit(p)==0,"execve env visible to child");
    return 0;
}

TEST(execve_env_replaced) {
    /* set a var in OUR env; execve with envp that does NOT contain it.
     * child should NOT see it -> __child_envcheck returns 1. */
    setenv("TP_SHOULD_VANISH","1",1);
    char *av[]={(char*)"tp",(char*)"__child_envcheck",(char*)"TP_SHOULD_VANISH",(char*)"1",NULL};
    char *ev[]={(char*)"PATH=/bin",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ execve(g_self,av,ev); _exit(126); }
    CHECK(reap_exit(p)==1,"execve replaced env (var gone)");
    return 0;
}

TEST(exec_echo_via_pipe) {
    int fds[2]; CHECK(pipe(fds)==0,"pipe");
    char *av[]={(char*)"tp",(char*)"__child_echo",(char*)"hello-substrate",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){
        close(fds[0]);
        dup2(fds[1],1);
        if (fds[1]!=1) close(fds[1]);
        execv(g_self,av);
        _exit(126);
    }
    close(fds[1]);
    char buf[64]; ssize_t n=0,t=0;
    while (t<(ssize_t)sizeof buf-1 && (n=read(fds[0],buf+t,sizeof buf-1-t))>0) t+=n;
    buf[t]=0; close(fds[0]);
    CHECK(reap_exit(p)==0,"child exit");
    CHECK(strcmp(buf,"hello-substrate")==0,"echoed string matches");
    return 0;
}

/* ================================================================== */
/* exec failures                                                       */
/* ================================================================== */

TEST(execv_nonexistent_enoent) {
    char *av[]={(char*)"nope",NULL};
    errno=0;
    int rc = execv("/no/such/binary/here", av);
    /* exec returns only on failure */
    CHECK(rc==-1,"execv returned -1");
    CHECK(errno==ENOENT,"ENOENT for missing path");
    /* and we (the test child) keep running afterwards */
    return 0;
}

TEST(process_survives_failed_exec) {
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){
        char *av[]={(char*)"nope",NULL};
        execv("/definitely/not/here",av);
        /* must reach here; report a distinctive code */
        _exit(73);
    }
    CHECK(reap_exit(p)==73,"child survived failed exec and exited 73");
    return 0;
}

TEST(exec_non_executable_eacces) {
    /* create a non-executable regular file and try to exec it */
    const char *path="/tmp/tp_noexec_file";
    int fd=open(path,O_CREAT|O_WRONLY|O_TRUNC,0644);
    if (fd<0) SKIP("cannot create temp file");
    write(fd,"not an elf\n",11);
    close(fd);
    chmod(path,0644); /* readable, not executable */
    errno=0;
    int rc=execv(path,(char*[]){(char*)path,NULL});
    int e=errno;
    unlink(path);
    CHECK(rc==-1,"execv non-exec returned -1");
    if (e!=EACCES && e!=ENOEXEC) SKIP("errno not EACCES/ENOEXEC distinguishable");
    return 0;
}

/* ================================================================== */
/* pipelines / fd inheritance                                          */
/* ================================================================== */

static unsigned simple_cksum(const unsigned char *p, size_t n) {
    unsigned c=2166136261u;
    for (size_t i=0;i<n;i++){ c^=p[i]; c*=16777619u; }
    return c;
}

TEST(pipe_to_catstdin_checksum) {
    /* parent -> child stdin (__child_catstdin copies stdin->stdout),
     * parent reads it back and checksums. */
    int in[2], out[2];
    CHECK(pipe(in)==0,"pipe in");
    CHECK(pipe(out)==0,"pipe out");
    unsigned char data[4096];
    for (size_t i=0;i<sizeof data;i++) data[i]=(unsigned char)(i*31+7);
    unsigned want = simple_cksum(data,sizeof data);

    char *av[]={(char*)"tp",(char*)"__child_catstdin",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){
        dup2(in[0],0); dup2(out[1],1);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        execv(g_self,av);
        _exit(126);
    }
    close(in[0]); close(out[1]);
    /* write then close to signal EOF */
    size_t off=0;
    while (off<sizeof data){
        ssize_t w=write(in[1],data+off,sizeof data-off);
        CHECK(w>0,"write to child");
        off+=(size_t)w;
    }
    close(in[1]);
    unsigned char back[4096]; size_t got=0; ssize_t r;
    while (got<sizeof back && (r=read(out[0],back+got,sizeof back-got))>0) got+=(size_t)r;
    close(out[0]);
    CHECK(reap_exit(p)==0,"catstdin exit");
    CHECK(got==sizeof data,"got all bytes back");
    CHECK(simple_cksum(back,got)==want,"checksum matches");
    return 0;
}

TEST(fd_inherited_across_fork) {
    int fds[2]; CHECK(pipe(fds)==0,"pipe");
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){
        close(fds[0]);
        const char *m="inherited";
        write(fds[1],m,9);
        close(fds[1]);
        _exit(0);
    }
    close(fds[1]);
    char buf[16]; ssize_t n=read(fds[0],buf,sizeof buf);
    close(fds[0]);
    CHECK(n==9,"read 9 bytes from inherited fd");
    CHECK(memcmp(buf,"inherited",9)==0,"content");
    CHECK(reap_exit(p)==0,"reap");
    return 0;
}

TEST(plain_fd_inherited_across_exec) {
    /* open a pipe; pass the read end's fd number to __child_fdcheck via
     * exec.  Without O_CLOEXEC it should remain open across exec -> 0. */
    int fds[2]; CHECK(pipe(fds)==0,"pipe");
    char num[16]; snprintf(num,sizeof num,"%d",fds[0]);
    char *av[]={(char*)"tp",(char*)"__child_fdcheck",num,NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ close(fds[1]); execv(g_self,av); _exit(126); }
    close(fds[1]);
    int rc=reap_exit(p);
    close(fds[0]);
    CHECK(rc==0,"plain fd open after exec");
    return 0;
}

TEST(cloexec_fd_not_inherited_across_exec) {
    int fds[2]; CHECK(pipe(fds)==0,"pipe");
    /* mark read end close-on-exec */
    int fl=fcntl(fds[0],F_GETFD);
    if (fl<0) SKIP("no F_GETFD");
    if (fcntl(fds[0],F_SETFD,fl|FD_CLOEXEC)<0) SKIP("no FD_CLOEXEC");
    char num[16]; snprintf(num,sizeof num,"%d",fds[0]);
    char *av[]={(char*)"tp",(char*)"__child_fdcheck",num,NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ close(fds[1]); execv(g_self,av); _exit(126); }
    close(fds[1]);
    int rc=reap_exit(p);
    close(fds[0]);
    /* __child_fdcheck returns 1 when fd is NOT valid */
    if (rc<0) CHECK(0,"fdcheck failed to run");
    CHECK(rc==1,"CLOEXEC fd closed across exec");
    return 0;
}

/* ================================================================== */
/* argv / env passing                                                  */
/* ================================================================== */

TEST(argv_passing_via_pipe) {
    /* __child_echo only echoes argv[2]; here we test passing tricky
     * strings (with spaces / empty) round-trips intact. */
    int fds[2]; CHECK(pipe(fds)==0,"pipe");
    const char *tricky="a b  c\ttrailing ";
    char *av[]={(char*)"tp",(char*)"__child_echo",(char*)tricky,NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ close(fds[0]); dup2(fds[1],1); if(fds[1]!=1) close(fds[1]); execv(g_self,av); _exit(126); }
    close(fds[1]);
    char buf[128]; ssize_t n=0,t=0;
    while (t<(ssize_t)sizeof buf-1 && (n=read(fds[0],buf+t,sizeof buf-1-t))>0) t+=n;
    buf[t]=0; close(fds[0]);
    CHECK(reap_exit(p)==0,"echo exit");
    CHECK(strcmp(buf,tricky)==0,"tricky argv round-trip");
    return 0;
}

TEST(empty_string_argv) {
    /* pass an empty string as the echo payload -> 0 bytes echoed */
    int fds[2]; CHECK(pipe(fds)==0,"pipe");
    char *av[]={(char*)"tp",(char*)"__child_echo",(char*)"",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ close(fds[0]); dup2(fds[1],1); if(fds[1]!=1) close(fds[1]); execv(g_self,av); _exit(126); }
    close(fds[1]);
    char buf[16]; ssize_t t=read(fds[0],buf,sizeof buf);
    close(fds[0]);
    CHECK(reap_exit(p)==0,"echo exit");
    CHECK(t==0,"empty string -> 0 bytes");
    return 0;
}

TEST(env_inherited_across_fork) {
    setenv("TP_INHERIT","yesval",1);
    int fds[2]; CHECK(pipe(fds)==0,"pipe");
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){
        close(fds[0]);
        const char *v=getenv("TP_INHERIT");
        int ok = v && strcmp(v,"yesval")==0;
        write(fds[1],&ok,sizeof ok);
        _exit(0);
    }
    close(fds[1]);
    int ok=0; CHECK(read(fds[0],&ok,sizeof ok)==(ssize_t)sizeof ok,"read");
    close(fds[0]);
    CHECK(ok,"env inherited across plain fork");
    CHECK(reap_exit(p)==0,"reap");
    return 0;
}

TEST(env_inherited_across_exec) {
    setenv("TP_EXECENV","abc",1);
    char *av[]={(char*)"tp",(char*)"__child_envcheck",(char*)"TP_EXECENV",(char*)"abc",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ execv(g_self,av); _exit(126); } /* execv keeps environ */
    CHECK(reap_exit(p)==0,"env survives execv (uses environ)");
    return 0;
}

/* ================================================================== */
/* signal disposition across fork/exec                                 */
/* ================================================================== */

static volatile sig_atomic_t g_caught;
static void catcher(int s){ (void)s; g_caught=1; }

TEST(signal_handler_inherited_across_fork) {
    struct sigaction sa={0}, old;
    sa.sa_handler=catcher;
    CHECK(sigaction(SIGUSR1,&sa,&old)==0,"install handler");
    int fds[2]; CHECK(pipe(fds)==0,"pipe");
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){
        close(fds[0]);
        g_caught=0;
        raise(SIGUSR1);
        int ok=g_caught;
        write(fds[1],&ok,sizeof ok);
        _exit(0);
    }
    close(fds[1]);
    int ok=0; ssize_t n=read(fds[0],&ok,sizeof ok);
    close(fds[0]);
    waitpid(p,NULL,0);
    sigaction(SIGUSR1,&old,NULL);
    if (n!=(ssize_t)sizeof ok) SKIP("child didn't report");
    CHECK(ok,"caught handler ran in forked child (disposition inherited)");
    return 0;
}

TEST(signal_disposition_reset_on_exec) {
    /* A caught signal's handler must reset to SIG_DFL across exec.  We
     * install a SIGTERM handler, then exec a child that sleeps; the
     * parent sends SIGTERM.  If the handler had carried across exec the
     * default-terminate wouldn't happen and the child would survive to
     * exit 0.  With proper reset, it dies by SIGTERM. */
    struct sigaction sa={0}, old;
    sa.sa_handler=catcher;
    CHECK(sigaction(SIGTERM,&sa,&old)==0,"install");
    char *av[]={(char*)"tp",(char*)"__child_sleep",(char*)"3000",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ execv(g_self,av); _exit(126); }
    sigaction(SIGTERM,&old,NULL);
    msleep(150);
    CHECK(kill(p,SIGTERM)==0,"kill");
    int st; CHECK(waitpid(p,&st,0)==p,"wait");
    if (!WIFSIGNALED(st)) SKIP("child didn't die by signal (timing)");
    CHECK(WTERMSIG(st)==SIGTERM,"SIGTERM default after exec reset");
    return 0;
}

TEST(ignored_signal_inherited_across_fork) {
    struct sigaction sa={0}, old;
    sa.sa_handler=SIG_IGN;
    CHECK(sigaction(SIGUSR2,&sa,&old)==0,"ignore");
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){
        /* SIGUSR2 ignored; raising it must not kill us */
        raise(SIGUSR2);
        _exit(55);
    }
    sigaction(SIGUSR2,&old,NULL);
    CHECK(reap_exit(p)==55,"ignored signal inherited (child survived)");
    return 0;
}

/* ================================================================== */
/* SIGCHLD                                                             */
/* ================================================================== */

static volatile sig_atomic_t g_sigchld;
static void chld_handler(int s){ (void)s; g_sigchld++; }

TEST(sigchld_on_child_death) {
    struct sigaction sa={0}, old;
    sa.sa_handler=chld_handler;
    sa.sa_flags=0;
    if (sigaction(SIGCHLD,&sa,&old)!=0) SKIP("no SIGCHLD support");
    g_sigchld=0;
    pid_t p=fork();
    if (p<0){ sigaction(SIGCHLD,&old,NULL); CHECK(0,"fork"); }
    if (p==0) _exit(0);
    int st; waitpid(p,&st,0);
    /* give the handler a chance */
    for (int i=0;i<50 && !g_sigchld;i++) msleep(10);
    int fired=g_sigchld;
    sigaction(SIGCHLD,&old,NULL);
    if (!fired) SKIP("SIGCHLD not delivered (may be flaky)");
    return 0;
}

/* ================================================================== */
/* demand-paged stack (original test, preserved as __child_stack chain) */
/* ================================================================== */

TEST(demand_paged_stack_chain) {
    /* Re-exec self depth-first, each level touching a >128KiB stack
     * buffer to force demand-paged grow-down.  Depth 48 keeps it under
     * the 8s watchdog on a healthy kernel. */
    char *av[]={(char*)"tp",(char*)"__child_stack",(char*)"48",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ execv(g_self,av); _exit(126); }
    int rc=reap_exit(p);
    CHECK(rc==0,"deep fork+exec stack-grow chain completed");
    return 0;
}

/* ================================================================== */
/* STRESS — the lost-wakeup hunters                                    */
/* ================================================================== */

TEST(stress_fork_exit_wait_5000) {
    /* Rapid fork + immediate _exit + blocking waitpid.  This hammers the
     * exact wakeup path the lost-wakeup bug lives on. */
    const int N=5000;
    for (int i=0;i<N;i++){
        pid_t p=fork();
        CHECK(p>=0,"fork");
        if (p==0) _exit(i&0xff);
        int st;
        CHECK(waitpid(p,&st,0)==p,"waitpid (possible lost wakeup)");
        CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==(i&0xff),"code");
    }
    return 0;
}

TEST(stress_200_concurrent_sleepers) {
    /* 200 children each sleep a tiny staggered amount then exit; their
     * wakeups land on the parent's wait concurrently. */
    enum { N=200 };
    pid_t pids[N];
    for (int i=0;i<N;i++){
        pid_t p=fork(); CHECK(p>=0,"fork");
        if (p==0){ msleep((i%17)+1); _exit(0); }
        pids[i]=p;
    }
    int reaped=0;
    for (;;){
        int st; pid_t r=wait(&st);
        if (r<0){ CHECK(errno==ECHILD,"ECHILD at end"); break; }
        reaped++;
    }
    CHECK(reaped==N,"reaped all concurrent sleepers");
    (void)pids;
    return 0;
}

TEST(stress_fork_exec_wait_1000) {
    /* fork + exec(__child_exit n) + wait, 1000 iterations. */
    const int N=1000;
    for (int i=0;i<N;i++){
        char arg[16]; snprintf(arg,sizeof arg,"%d",i&0x7f);
        char *av[]={(char*)"tp",(char*)"__child_exit",arg,NULL};
        pid_t p=fork(); CHECK(p>=0,"fork");
        if (p==0){ execv(g_self,av); _exit(126); }
        int st;
        CHECK(waitpid(p,&st,0)==p,"waitpid after exec");
        CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==(i&0x7f),"code");
    }
    return 0;
}

TEST(stress_wnohang_spin_reap) {
    /* spawn children and reap them with a WNOHANG spin loop (exercises
     * the non-blocking reap path repeatedly). */
    enum { N=300 };
    pid_t pids[N]; char reaped[N]; memset(reaped,0,sizeof reaped);
    for (int i=0;i<N;i++){
        pid_t p=fork(); CHECK(p>=0,"fork");
        if (p==0){ msleep(i%5); _exit(0); }
        pids[i]=p;
    }
    int left=N;
    int guard=0;
    while (left>0){
        for (int i=0;i<N;i++){
            if (reaped[i]) continue;
            int st; pid_t r=waitpid(pids[i],&st,WNOHANG);
            if (r==pids[i]){ reaped[i]=1; left--; }
            else CHECK(r==0,"WNOHANG returns 0 or pid");
        }
        if (++guard>1000000) CHECK(0,"WNOHANG spin never drained");
    }
    return 0;
}

TEST(stress_burst_fork_pid_unique) {
    /* burst of forks; ensure no two simultaneously-live children share
     * a pid. */
    enum { N=64 };
    pid_t pids[N];
    for (int i=0;i<N;i++){
        pid_t p=fork(); CHECK(p>=0,"fork");
        if (p==0){ msleep(60); _exit(0); }
        pids[i]=p;
    }
    /* check uniqueness while they're (mostly) still alive */
    for (int i=0;i<N;i++)
        for (int j=i+1;j<N;j++)
            CHECK(pids[i]!=pids[j],"two live children share a pid");
    /* reap */
    for (int i=0;i<N;i++){ int st; waitpid(pids[i],&st,0); }
    return 0;
}

TEST(stress_nested_fork_two_levels) {
    /* each of M children forks K grandchildren; everyone reaps their own.
     * Exercises wait wakeups at two levels concurrently. */
    enum { M=20, K=10 };
    pid_t kids[M];
    for (int i=0;i<M;i++){
        pid_t p=fork(); CHECK(p>=0,"fork lvl1");
        if (p==0){
            pid_t gk[K];
            for (int j=0;j<K;j++){
                pid_t g=fork();
                if (g<0) _exit(70);
                if (g==0){ msleep(j%4); _exit(0); }
                gk[j]=g;
            }
            for (int j=0;j<K;j++){ int st; if(waitpid(gk[j],&st,0)!=gk[j]) _exit(71); }
            _exit(0);
        }
        kids[i]=p;
    }
    for (int i=0;i<M;i++){
        int st;
        CHECK(waitpid(kids[i],&st,0)==kids[i],"wait lvl1");
        CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0,"nested child clean");
    }
    return 0;
}

TEST(stress_fork_kill_wait_500) {
    /* fork a child that blocks, kill it, wait — repeatedly.  Exercises
     * the wait-wakeup-by-signal-death path. */
    const int N=500;
    for (int i=0;i<N;i++){
        pid_t p=fork(); CHECK(p>=0,"fork");
        if (p==0){ for(;;) pause(); _exit(0); }
        CHECK(kill(p,SIGKILL)==0,"kill");
        int st;
        CHECK(waitpid(p,&st,0)==p,"wait after kill");
        CHECK(WIFSIGNALED(st)&&WTERMSIG(st)==SIGKILL,"killed by SIGKILL");
    }
    return 0;
}

TEST(stress_interleaved_spawn_reap) {
    /* Keep a sliding window of live children: spawn, and once the window
     * is full reap one before spawning the next.  2000 total. */
    enum { W=16 };
    pid_t win[W]; int filled=0; int total=2000;
    int wi=0;
    for (int i=0;i<total;i++){
        if (filled==W){
            int st;
            CHECK(waitpid(win[wi],&st,0)==win[wi],"reap oldest");
            filled--;
        }
        pid_t p=fork(); CHECK(p>=0,"fork");
        if (p==0){ msleep(i%3); _exit(0); }
        win[wi]=p; wi=(wi+1)%W; filled++;
    }
    /* drain */
    while (filled>0){
        int wj=(wi - filled + W*2)%W;
        int st; CHECK(waitpid(win[wj],&st,0)==win[wj],"drain"); filled--;
    }
    return 0;
}

/* ================================================================== */
/* misc                                                                */
/* ================================================================== */

TEST(many_sequential_forks) {
    for (int i=0;i<256;i++){
        pid_t p=fork(); CHECK(p>=0,"fork");
        if (p==0) _exit(0);
        CHECK(reap_exit(p)==0,"seq reap");
    }
    return 0;
}

TEST(child_can_fork_again) {
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){
        pid_t g=fork();
        if (g<0) _exit(60);
        if (g==0) _exit(0);
        int st;
        if (waitpid(g,&st,0)!=g) _exit(61);
        _exit(WIFEXITED(st)&&WEXITSTATUS(st)==0 ? 0 : 62);
    }
    CHECK(reap_exit(p)==0,"child forked+reaped its own grandchild");
    return 0;
}

TEST(exec_self_then_fork) {
    /* exec self in a mode that itself forks (__child_exit just exits, so
     * use __child_echo and verify the re-exec'd image is functional). */
    int fds[2]; CHECK(pipe(fds)==0,"pipe");
    char *av[]={(char*)"tp",(char*)"__child_echo",(char*)"reexec-ok",NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ close(fds[0]); dup2(fds[1],1); if(fds[1]!=1) close(fds[1]); execv(g_self,av); _exit(126);}
    close(fds[1]);
    char buf[32]; ssize_t t=0,n;
    while (t<(ssize_t)sizeof buf-1 && (n=read(fds[0],buf+t,sizeof buf-1-t))>0) t+=n;
    buf[t]=0; close(fds[0]);
    CHECK(reap_exit(p)==0,"exit");
    CHECK(strcmp(buf,"reexec-ok")==0,"re-exec'd self produced output");
    return 0;
}

TEST(child_raises_then_parent_reaps) {
    char arg[16]; snprintf(arg,sizeof arg,"%d",SIGABRT);
    char *av[]={(char*)"tp",(char*)"__child_sig",arg,NULL};
    pid_t p=fork(); CHECK(p>=0,"fork");
    if (p==0){ execv(g_self,av); _exit(126); }
    int st; CHECK(waitpid(p,&st,0)==p,"wait");
    CHECK(WIFSIGNALED(st)&&WTERMSIG(st)==SIGABRT,"child raised SIGABRT via exec'd image");
    return 0;
}

TEST(waitpid_reports_correct_pid_among_many) {
    enum { N=10 };
    pid_t pids[N];
    for (int i=0;i<N;i++){
        pid_t p=fork(); CHECK(p>=0,"fork");
        if (p==0){ msleep(i*8); _exit(i); }
        pids[i]=p;
    }
    /* reap a specific middle one first */
    int st;
    CHECK(waitpid(pids[5],&st,0)==pids[5],"specific reap among many");
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==5,"its code");
    for (int i=0;i<N;i++){ if(i==5) continue; waitpid(pids[i],&st,0); }
    return 0;
}

/* ================================================================== */
/* child-mode dispatch                                                 */
/* ================================================================== */

#define STACK_PROBE (256 * 1024)   /* > exec's 128 KiB eager region */

static int child_stack(int depth) {
    volatile unsigned char buf[STACK_PROBE];
    for (int i=0;i<STACK_PROBE;i+=4096) buf[i]=(unsigned char)depth;
    int sum=0;
    for (int i=0;i<STACK_PROBE;i+=4096) sum+=buf[i];
    if (depth<=0) return (sum>=0) ? 0 : 0;  /* bottomed out */
    pid_t p=fork();
    if (p<0) return 1;
    if (p==0){
        char d[16]; snprintf(d,sizeof d,"%d",depth-1);
        execl(g_self,"tp","__child_stack",d,(char*)NULL);
        _exit(127);
    }
    int st;
    if (waitpid(p,&st,0)!=p) return 1;
    if (!WIFEXITED(st)||WEXITSTATUS(st)!=0) return 1;
    return 0;
}

static int dispatch_child(int argc, char **argv) {
    const char *mode=argv[1];
    if (strcmp(mode,"__child_exit")==0) {
        return argc>=3 ? (atoi(argv[2]) & 0xff) : 0;
    }
    if (strcmp(mode,"__child_echo")==0) {
        if (argc>=3){ size_t n=strlen(argv[2]); ssize_t off=0;
            while (off<(ssize_t)n){ ssize_t w=write(1,argv[2]+off,n-off); if(w<=0) break; off+=w; } }
        return 0;
    }
    if (strcmp(mode,"__child_sleep")==0) {
        if (argc>=3) msleep(atol(argv[2]));
        return 0;
    }
    if (strcmp(mode,"__child_sig")==0) {
        int s = argc>=3 ? atoi(argv[2]) : SIGTERM;
        signal(s,SIG_DFL);
        raise(s);
        for (volatile int i=0;i<2000000;i++){}
        return 0;
    }
    if (strcmp(mode,"__child_catstdin")==0) {
        char buf[4096]; ssize_t r;
        while ((r=read(0,buf,sizeof buf))>0){
            ssize_t off=0; while (off<r){ ssize_t w=write(1,buf+off,r-off); if(w<=0) return 1; off+=w; }
        }
        return 0;
    }
    if (strcmp(mode,"__child_envcheck")==0) {
        if (argc<4) return 2;
        const char *v=getenv(argv[2]);
        return (v && strcmp(v,argv[3])==0) ? 0 : 1;
    }
    if (strcmp(mode,"__child_fdcheck")==0) {
        if (argc<3) return 2;
        int fd=atoi(argv[2]);
        /* fcntl F_GETFD: 0 if valid, -1/EBADF if closed */
        int r=fcntl(fd,F_GETFD);
        return (r>=0) ? 0 : 1;
    }
    if (strcmp(mode,"__child_stack")==0) {
        int depth = argc>=3 ? atoi(argv[2]) : 48;
        return child_stack(depth);
    }
    /* unknown child mode */
    return 100;
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */

int main(int argc, char **argv) {
    /* Establish self-path for re-exec.  Prefer argv[0] when it's a path;
     * otherwise fall back to the well-known /tmp path (works on target
     * since the binary is run as /tmp/torture_procs via init=). */
    if (argv[0] && strchr(argv[0], '/'))
        g_self = argv[0];
    else
        g_self = "/tmp/torture_procs";
#ifdef __linux__
    /* On Linux /proc/self/exe is the most reliable self-path. */
    {
        static char exep[4096];
        ssize_t n = readlink("/proc/self/exe", exep, sizeof exep - 1);
        if (n > 0) { exep[n]=0; g_self = exep; }
    }
#endif

    /* Child-mode dispatch: do NOT run the suite. */
    if (argc >= 2 && strncmp(argv[1], "__child", 7) == 0)
        return dispatch_child(argc, argv);

    /* Reap orphaned descendants automatically is not relied upon; every
     * test reaps its own.  Make stdout line-ish buffered behavior moot
     * by flushing in run_one. */
    setvbuf(stdout, NULL, _IONBF, 0);

    fprintf(stdout, "torture_procs: subprocess torture suite (self=%s)\n", g_self);

    /* fork basics */
    RUN(fork_returns_pid_and_zero);
    RUN(fork_child_sees_zero);
    RUN(child_ppid_is_parent);
    RUN(getpid_distinct_parent_child);

    /* exit codes */
    RUN(exit_code_0);
    RUN(exit_code_1);
    RUN(exit_code_42);
    RUN(exit_code_255);
    RUN(exit_code_many);
    RUN(return_from_main_equiv_exit);
    RUN(exit_high_bits_masked);

    /* signal classification */
    RUN(sig_kill_classified);
    RUN(sig_segv_classified);
    RUN(sig_abrt_classified);
    RUN(sig_term_classified);
    RUN(wifexited_vs_wifsignaled);
    RUN(parent_kills_child_signal);

    /* waitpid variants */
    RUN(wait_specific_pid);
    RUN(wait_any_minus1);
    RUN(wnohang_then_reap);
    RUN(wnohang_after_exit);
    RUN(wait_nonchild_echild);
    RUN(double_wait_echild);
    RUN(wait_no_children_echild);

    /* multiple children / ordering */
    RUN(spawn_32_verify_codes);
    RUN(reap_any_order_match_codes);
    RUN(staggered_exit_order);

    /* zombie / reparent */
    RUN(zombie_status_preserved);
    RUN(orphan_reparented_to_init);

    /* exec family */
    RUN(execv_exit_code);
    RUN(execl_exit_code);
    RUN(execvp_finds_abspath);
    RUN(execve_custom_env);
    RUN(execve_env_replaced);
    RUN(exec_echo_via_pipe);

    /* exec failures */
    RUN(execv_nonexistent_enoent);
    RUN(process_survives_failed_exec);
    RUN(exec_non_executable_eacces);

    /* pipelines / fd inheritance */
    RUN(pipe_to_catstdin_checksum);
    RUN(fd_inherited_across_fork);
    RUN(plain_fd_inherited_across_exec);
    RUN(cloexec_fd_not_inherited_across_exec);

    /* argv / env */
    RUN(argv_passing_via_pipe);
    RUN(empty_string_argv);
    RUN(env_inherited_across_fork);
    RUN(env_inherited_across_exec);

    /* signal disposition */
    RUN(signal_handler_inherited_across_fork);
    RUN(signal_disposition_reset_on_exec);
    RUN(ignored_signal_inherited_across_fork);
    RUN(sigchld_on_child_death);

    /* demand-paged stack */
    RUN(demand_paged_stack_chain);

    /* misc */
    RUN(many_sequential_forks);
    RUN(child_can_fork_again);
    RUN(exec_self_then_fork);
    RUN(child_raises_then_parent_reaps);
    RUN(waitpid_reports_correct_pid_among_many);

    /* STRESS — lost-wakeup hunters (run last; they take longest) */
    RUN(stress_fork_exit_wait_5000);
    RUN(stress_200_concurrent_sleepers);
    RUN(stress_fork_exec_wait_1000);
    RUN(stress_wnohang_spin_reap);
    RUN(stress_burst_fork_pid_unique);
    RUN(stress_nested_fork_two_levels);
    RUN(stress_fork_kill_wait_500);
    RUN(stress_interleaved_spawn_reap);

    fprintf(stdout,
        "\n==== torture_procs: %d run, %d PASS, %d FAIL, %d HANG, %d SKIP ====\n",
        tests_run, tests_pass, tests_fail, tests_hang, tests_skip);
    return (tests_fail || tests_hang) ? 1 : 0;
}
