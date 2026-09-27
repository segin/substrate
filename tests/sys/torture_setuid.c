/*
 * torture_setuid.c -- set-user-ID and set-group-ID execution.
 *
 * Installs copies of setuid_helper (argv[1]; it prints the credentials it
 * received) under different owners and modes, runs them as uid/gid 1000,
 * and checks each result:
 *
 *   plain      no bits: nothing changes, AT_SECURE 0;
 *   suid-root  4755 root: euid 0, uid 1000, AT_SECURE 1, the saved uid
 *              lets it drop and regain root; setuid(1000) is permanent;
 *   suid-user  4755 owned by 2000: euid 2000;
 *   sgid       2755 group 42: egid 42, gid 1000, AT_SECURE 1;
 *   root-exec  root running the 2000-owned copy: euid 2000, uid 0;
 *   script     a 4755 root "#!" script: the bit is ignored (as on Linux,
 *              BSD) -- honouring it is a classic race;
 *   preload    LD_PRELOAD reaches a plain program but not a setuid one;
 *   ptrace     uid 1000 cannot PTRACE_ATTACH to a running setuid-root
 *              process, and a traced exec does not gain root;
 *   nosuid     on a filesystem mounted nosuid the bit is ignored (needs a
 *              second disk labelled "noatimetest", as torture_mount_noatime).
 *
 * Runs as init (root): torture_setuid [helper [preload.so]], by default
 * /setuid_helper and /setuid_preload.so (built from setuid_preload.c).
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define USER 1000

static int failures;
static const char *helper_src;

static void check(const char *name, int ok, const char *got) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : got);
    if (!ok)
        failures++;
}

static int copy(const char *from, const char *to) {
    char buf[8192];
    ssize_t n;
    int in = open(from, O_RDONLY);
    unlink(to);
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (in < 0 || out < 0)
        return -1;
    while ((n = read(in, buf, sizeof(buf))) > 0)
        write(out, buf, (size_t)n);
    close(in);
    close(out);
    return 0;
}

static void install(const char *path, uid_t uid, gid_t gid, mode_t mode) {
    copy(helper_src, path);
    chown(path, uid, gid);
    chmod(path, mode);                  /* after chown, which may clear bits */
}

/*
 * Run `prog arg` as `uid` (-1: stay root) with LD_PRELOAD=`preload` (or
 * none) and capture its output into out[].
 */
static void run(char *out, size_t outsz, int uid, const char *prog,
                const char *arg, const char *preload) {
    int p[2];
    pipe(p);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[0]);
        close(p[1]);
        if (uid >= 0 && (setgid(USER) != 0 || setuid((uid_t)uid) != 0))
            _exit(99);
        if (preload)
            setenv("LD_PRELOAD", preload, 1);
        execl(prog, prog, arg, (char *)NULL);
        _exit(127);
    }
    close(p[1]);
    size_t n = 0;
    ssize_t r;
    while (n < outsz - 1 && (r = read(p[0], out + n, outsz - 1 - n)) > 0)
        n += (size_t)r;
    out[n] = '\0';
    close(p[0]);
    waitpid(pid, NULL, 0);
}

static int has(const char *out, const char *s) { return strstr(out, s) != NULL; }

int main(int argc, char **argv) {
    char out[1024];
    helper_src = argc > 1 ? argv[1] : "/setuid_helper";
    const char *preload = argc > 2 ? argv[2] : "/setuid_preload.so";
    printf("torture_setuid\n");
    mkdir("/tmp", 01777);

    install("/tmp/su-plain", 0, 0, 0755);
    run(out, sizeof(out), USER, "/tmp/su-plain", NULL, NULL);
    check("plain: credentials unchanged",
          has(out, "uid=1000 euid=1000 gid=1000 egid=1000 secure=0"), out);

    install("/tmp/su-root", 0, 0, 04755);
    run(out, sizeof(out), USER, "/tmp/su-root", "drop", NULL);
    check("suid-root: euid 0, uid 1000, AT_SECURE 1",
          has(out, "uid=1000 euid=0 gid=1000 egid=1000 secure=1"), out);
    check("suid-root: saved uid lets it drop and regain root", has(out, "saved=1"), out);
    check("suid-root: setuid(uid) is permanent", has(out, "REGAIN=0"), out);

    install("/tmp/su-2000", 2000, 2000, 04755);
    run(out, sizeof(out), USER, "/tmp/su-2000", NULL, NULL);
    check("suid-user: euid 2000", has(out, "uid=1000 euid=2000"), out);

    install("/tmp/sg-42", 0, 42, 02755);
    run(out, sizeof(out), USER, "/tmp/sg-42", NULL, NULL);
    check("sgid: egid 42, gid 1000, AT_SECURE 1",
          has(out, "uid=1000 euid=1000 gid=1000 egid=42 secure=1"), out);

    run(out, sizeof(out), -1, "/tmp/su-2000", NULL, NULL);
    check("root-exec: root running a 2000-owned setuid program gets euid 2000",
          has(out, "uid=0 euid=2000"), out);

    {
        int fd = open("/tmp/su-script", O_WRONLY | O_CREAT | O_TRUNC, 0755);
        dprintf(fd, "#!/tmp/su-plain\n");
        close(fd);
        chown("/tmp/su-script", 0, 0);
        chmod("/tmp/su-script", 04755);
        run(out, sizeof(out), USER, "/tmp/su-script", NULL, NULL);
        check("script: a setuid #! script gains nothing",
              has(out, "euid=1000") && !has(out, "euid=0"), out);
    }

    run(out, sizeof(out), USER, "/tmp/su-plain", NULL, preload);
    check("preload: LD_PRELOAD reaches a plain program", has(out, "PRELOADED"), out);
    run(out, sizeof(out), USER, "/tmp/su-root", NULL, preload);
    check("preload: LD_PRELOAD is ignored for a setuid program",
          !has(out, "PRELOADED") && has(out, "euid=0"), out);

    /* ptrace: attach to a running setuid-root process as its real user. */
    {
        pid_t target = fork();
        if (target == 0) {
            int null = open("/dev/null", O_WRONLY);
            dup2(null, 1);
            setgid(USER);
            setuid(USER);
            execl("/tmp/su-root", "su-root", "sleep", (char *)NULL);
            _exit(127);
        }
        sleep(1);
        pid_t att = fork();
        if (att == 0) {
            setgid(USER);
            setuid(USER);
            errno = 0;
            long r = ptrace(PTRACE_ATTACH, target, NULL, NULL);
            if (r == 0) {
                ptrace(PTRACE_DETACH, target, NULL, NULL);
                _exit(1);
            }
            _exit(errno == EPERM ? 0 : 2);
        }
        int st;
        waitpid(att, &st, 0);
        char why[64];
        snprintf(why, sizeof(why), "attach status %d", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        check("ptrace: uid 1000 cannot attach to a setuid-root process",
              WIFEXITED(st) && WEXITSTATUS(st) == 0, why);
        kill(target, SIGKILL);
        waitpid(target, NULL, 0);
    }
    {
        /* The tracer must be unprivileged (a root tracer may do anything
         * already): an intermediate process drops to uid 1000 and traces
         * its own child, which PTRACE_TRACEMEs and execs su-root. */
        int p[2];
        pipe(p);
        pid_t tracer = fork();
        if (tracer == 0) {
            close(p[0]);
            setgid(USER);
            setuid(USER);
            pid_t child = fork();
            if (child == 0) {
                dup2(p[1], 1);
                close(p[1]);
                ptrace(PTRACE_TRACEME, 0, NULL, NULL);
                execl("/tmp/su-root", "su-root", (char *)NULL);
                _exit(127);
            }
            close(p[1]);
            int st;
            /* The exec stops the tracee with SIGTRAP; let it run on. */
            for (int i = 0; i < 4; i++) {
                if (waitpid(child, &st, 0) != child || !WIFSTOPPED(st))
                    break;
                ptrace(PTRACE_CONT, child, NULL, NULL);
            }
            _exit(0);
        }
        close(p[1]);
        size_t n = 0;
        ssize_t r;
        while (n < sizeof(out) - 1 && (r = read(p[0], out + n, sizeof(out) - 1 - n)) > 0)
            n += (size_t)r;
        out[n] = '\0';
        close(p[0]);
        waitpid(tracer, NULL, 0);
        check("ptrace: a traced exec of a setuid-root program does not gain root",
              has(out, "CREDS") && !has(out, "euid=0"), out[0] ? out : "(no output)");
    }

    /* nosuid: the second disk, mounted nosuid. */
    mkdir("/mnt", 0755);
    mkdir("/mnt/nosuid", 0755);
    if (mount("LABEL=noatimetest", "/mnt/nosuid", "ext2", MNT_NOSUID, NULL) == 0) {
        install("/mnt/nosuid/su-root", 0, 0, 04755);
        run(out, sizeof(out), USER, "/mnt/nosuid/su-root", NULL, NULL);
        check("nosuid: the bit is ignored on a nosuid mount",
              has(out, "euid=1000") && !has(out, "euid=0"), out);
        unlink("/mnt/nosuid/su-root");
        umount("/mnt/nosuid");
    } else {
        printf("  skip nosuid: no second disk (%s)\n", strerror(errno));
    }

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
