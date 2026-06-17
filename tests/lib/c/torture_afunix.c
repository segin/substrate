/*
 * torture_afunix.c — AF_UNIX filesystem-node + permission torture.
 *
 * torture_unix.c already covers the data path (socketpair, connect/
 * accept, send/recv/sendmsg, shutdown, large payloads).  This battery
 * targets the *filesystem* side of pathname-bound AF_UNIX sockets — the
 * area substrate's `af_unix.c` is weakest on and the one that breaks
 * real session managers.
 *
 * Motivating bug: TDE's tdeinit binds /tmp/tdesocket-<user>/tdeinit__0,
 * then chmod()s and re-stat()s it as a security check, and aborts with
 *     [tdeinit] Aborting. Can't set permissions on socket: : error 2
 * (errno 2 == ENOENT).  That means bind() did not leave a stat()-able
 * S_ISSOCK node in the filesystem, or chmod()/stat() on it fails.  The
 * `tdeinit_socket_pattern` scenario reproduces that dance exactly.
 *
 * Portable POSIX C: builds and passes on Linux/BSD with the host
 * toolchain (the baseline), and cross-builds against substrate's libc
 * with CROSS=/opt/substrate/bin/i386-unknown-substrate-.  Each scenario
 * prints PASS / FAIL; the program exits 0 iff every scenario passed.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#define MUST(cond, msg) do {                                          \
    if (!(cond)) {                                                    \
        fprintf(stderr, "  FAIL: %s (errno=%d: %s)\n",                \
                (msg), errno, strerror(errno));                       \
        goto fail;                                                    \
    }                                                                 \
} while (0)

/* Expect a call to fail with a specific errno. */
#define MUST_FAIL(expr, want, msg) do {                               \
    errno = 0;                                                        \
    if ((expr) != -1) {                                               \
        fprintf(stderr, "  FAIL: %s (expected -1/%s, got success)\n", \
                (msg), #want);                                        \
        goto fail;                                                    \
    }                                                                 \
    if (errno != (want)) {                                            \
        fprintf(stderr, "  FAIL: %s (errno=%d: %s, wanted %s)\n",     \
                (msg), errno, strerror(errno), #want);                \
        goto fail;                                                    \
    }                                                                 \
} while (0)

#define TEST(name) static int test_##name(void)
#define RUN(name) do {                                                \
    fprintf(stdout, "[%2d/%2d] %-26s ", ++tests_run, TOTAL, #name);   \
    fflush(stdout);                                                   \
    int rc = test_##name();                                           \
    if (rc == 0) { fprintf(stdout, "PASS\n"); tests_pass++; }         \
    else         { fprintf(stdout, "  -> FAILED\n"); tests_fail++; }  \
} while (0)

static const int TOTAL = 13;

/* Build a unique /tmp path per scenario so a stale node never masks a
 * result and parallel runs do not collide. */
static void mkpath(struct sockaddr_un *sun, const char *tag)
{
    memset(sun, 0, sizeof(*sun));
    sun->sun_family = AF_UNIX;
    snprintf(sun->sun_path, sizeof(sun->sun_path),
             "/tmp/tafx-%ld-%s.sock", (long)getpid(), tag);
}

/* ------------------------------------------------------------------ */

/* bind() must leave a stat()-able S_ISSOCK node in the filesystem. */
TEST(bind_creates_fs_node)
{
    struct sockaddr_un a; mkpath(&a, "node");
    unlink(a.sun_path);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(s >= 0, "socket");
    MUST(bind(s, (struct sockaddr *)&a, sizeof(a)) == 0, "bind");

    struct stat st;
    MUST(lstat(a.sun_path, &st) == 0, "lstat bound path");
    MUST(S_ISSOCK(st.st_mode), "bound path is a socket node");

    close(s); unlink(a.sun_path);
    return 0;
fail:
    close(s); unlink(a.sun_path);
    return -1;
}

/* fstat() of the socket fd reports a socket. */
TEST(fstat_socket_fd)
{
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(s >= 0, "socket");
    struct stat st;
    MUST(fstat(s, &st) == 0, "fstat socket fd");
    MUST(S_ISSOCK(st.st_mode), "fd stat is a socket");
    close(s);
    return 0;
fail:
    close(s);
    return -1;
}

/* chmod() the bound socket path, then read the mode back.  This is the
 * exact operation tdeinit performs and reports failing. */
TEST(chmod_socket_path)
{
    struct sockaddr_un a; mkpath(&a, "chmod");
    unlink(a.sun_path);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(s >= 0, "socket");
    MUST(bind(s, (struct sockaddr *)&a, sizeof(a)) == 0, "bind");

    MUST(chmod(a.sun_path, 0600) == 0, "chmod 0600 on socket path");
    struct stat st;
    MUST(stat(a.sun_path, &st) == 0, "stat after chmod");
    MUST((st.st_mode & 0777) == 0600, "mode bits == 0600 after chmod");

    close(s); unlink(a.sun_path);
    return 0;
fail:
    close(s); unlink(a.sun_path);
    return -1;
}

/* fchmod() via the socket fd changes the socket inode's own mode, as
 * seen through fstat() on that fd.  (Note: POSIX does not require the
 * change to propagate to a bound pathname's directory entry — Linux
 * does not — so this asserts the fd's view, not stat(path).) */
TEST(fchmod_socket_fd)
{
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(s >= 0, "socket");
    MUST(fchmod(s, 0660) == 0, "fchmod 0660 on socket fd");
    struct stat st;
    MUST(fstat(s, &st) == 0, "fstat after fchmod");
    MUST((st.st_mode & 0777) == 0660, "fd mode bits == 0660 after fchmod");
    close(s);
    return 0;
fail:
    close(s);
    return -1;
}

/* access(path, F_OK) succeeds on a bound socket node. */
TEST(access_socket_path)
{
    struct sockaddr_un a; mkpath(&a, "access");
    unlink(a.sun_path);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(s >= 0, "socket");
    MUST(bind(s, (struct sockaddr *)&a, sizeof(a)) == 0, "bind");
    MUST(access(a.sun_path, F_OK) == 0, "access(F_OK) on socket node");
    close(s); unlink(a.sun_path);
    return 0;
fail:
    close(s); unlink(a.sun_path);
    return -1;
}

/* Re-binding a second socket to an occupied path fails with EADDRINUSE. */
TEST(bind_eaddrinuse)
{
    struct sockaddr_un a; mkpath(&a, "inuse");
    unlink(a.sun_path);
    int s1 = socket(AF_UNIX, SOCK_STREAM, 0);
    int s2 = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(s1 >= 0 && s2 >= 0, "socket x2");
    MUST(bind(s1, (struct sockaddr *)&a, sizeof(a)) == 0, "first bind");
    MUST_FAIL(bind(s2, (struct sockaddr *)&a, sizeof(a)), EADDRINUSE,
              "second bind to same path");
    close(s1); close(s2); unlink(a.sun_path);
    return 0;
fail:
    close(s1); close(s2); unlink(a.sun_path);
    return -1;
}

/* connect() to a path that does not exist fails with ENOENT. */
TEST(connect_enoent)
{
    struct sockaddr_un a; mkpath(&a, "noent");
    unlink(a.sun_path);
    int c = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(c >= 0, "socket");
    MUST_FAIL(connect(c, (struct sockaddr *)&a, sizeof(a)), ENOENT,
              "connect to nonexistent path");
    close(c);
    return 0;
fail:
    close(c);
    return -1;
}

/* connect() to a socket node with no listener fails with ECONNREFUSED. */
TEST(connect_no_listener)
{
    struct sockaddr_un a; mkpath(&a, "nolisten");
    unlink(a.sun_path);
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    int c = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(srv >= 0 && c >= 0, "socket x2");
    MUST(bind(srv, (struct sockaddr *)&a, sizeof(a)) == 0, "bind (no listen)");
    MUST_FAIL(connect(c, (struct sockaddr *)&a, sizeof(a)), ECONNREFUSED,
              "connect to bound-but-unlistening socket");
    close(srv); close(c); unlink(a.sun_path);
    return 0;
fail:
    close(srv); close(c); unlink(a.sun_path);
    return -1;
}

/* connect() to a plain regular file fails with ECONNREFUSED or ENOTSOCK. */
TEST(connect_not_a_socket)
{
    struct sockaddr_un a; mkpath(&a, "regfile");
    unlink(a.sun_path);
    int c = -1;
    int fd = open(a.sun_path, O_CREAT | O_WRONLY, 0600);
    MUST(fd >= 0, "create regular file");
    close(fd);
    c = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(c >= 0, "socket");
    errno = 0;
    int rc = connect(c, (struct sockaddr *)&a, sizeof(a));
    MUST(rc == -1, "connect to regular file must fail");
    MUST(errno == ECONNREFUSED || errno == ENOTSOCK,
         "connect to regular file gives ECONNREFUSED/ENOTSOCK");
    close(c); unlink(a.sun_path);
    return 0;
fail:
    close(c); unlink(a.sun_path);
    return -1;
}

/* The server-restart pattern: unlink the stale node, rebind, connect. */
TEST(unlink_rebind)
{
    struct sockaddr_un a; mkpath(&a, "rebind");
    unlink(a.sun_path);
    int s1 = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(s1 >= 0, "socket 1");
    MUST(bind(s1, (struct sockaddr *)&a, sizeof(a)) == 0, "bind 1");
    close(s1);                              /* leaves the stale node */

    MUST(unlink(a.sun_path) == 0, "unlink stale node");
    int s2 = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(s2 >= 0, "socket 2");
    MUST(bind(s2, (struct sockaddr *)&a, sizeof(a)) == 0, "rebind after unlink");
    MUST(listen(s2, 4) == 0, "listen after rebind");

    int c = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(c >= 0, "client socket");
    MUST(connect(c, (struct sockaddr *)&a, sizeof(a)) == 0, "connect after rebind");
    close(c); close(s2); unlink(a.sun_path);
    return 0;
fail:
    unlink(a.sun_path);
    return -1;
}

/* SCM_RIGHTS: pass an open fd across a socketpair and use it. */
TEST(scm_rights_fd_passing)
{
    int sv[2] = { -1, -1 };
    int passed = -1;
    /* Deterministic temp path (not mkstemp): mkstemp draws from the
     * kernel RNG, which blocks on a headless boot without an entropy
     * source, and that has nothing to do with what we are testing. */
    char tmpl[64];
    snprintf(tmpl, sizeof(tmpl), "/tmp/tafx-%ld-scm.dat", (long)getpid());
    unlink(tmpl);
    int srcfd = open(tmpl, O_CREAT | O_EXCL | O_RDWR, 0600);
    MUST(srcfd >= 0, "open temp file");
    MUST(write(srcfd, "scm-rights-payload", 18) == 18, "write payload");

    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    /* Send srcfd over sv[0]. */
    char iobuf = 'F';
    struct iovec iov = { &iobuf, 1 };
    union { struct cmsghdr h; char buf[CMSG_SPACE(sizeof(int))]; } cm;
    memset(&cm, 0, sizeof(cm));
    struct msghdr mh; memset(&mh, 0, sizeof(mh));
    mh.msg_iov = &iov; mh.msg_iovlen = 1;
    mh.msg_control = cm.buf; mh.msg_controllen = sizeof(cm.buf);
    struct cmsghdr *c = CMSG_FIRSTHDR(&mh);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &srcfd, sizeof(int));
    MUST(sendmsg(sv[0], &mh, 0) == 1, "sendmsg with SCM_RIGHTS");

    /* Receive it on sv[1]. */
    char rbuf = 0;
    struct iovec riov = { &rbuf, 1 };
    union { struct cmsghdr h; char buf[CMSG_SPACE(sizeof(int))]; } rcm;
    memset(&rcm, 0, sizeof(rcm));
    struct msghdr rmh; memset(&rmh, 0, sizeof(rmh));
    rmh.msg_iov = &riov; rmh.msg_iovlen = 1;
    rmh.msg_control = rcm.buf; rmh.msg_controllen = sizeof(rcm.buf);
    MUST(recvmsg(sv[1], &rmh, 0) == 1, "recvmsg with SCM_RIGHTS");
    struct cmsghdr *rc = CMSG_FIRSTHDR(&rmh);
    MUST(rc && rc->cmsg_type == SCM_RIGHTS, "received SCM_RIGHTS cmsg");
    memcpy(&passed, CMSG_DATA(rc), sizeof(int));
    MUST(passed >= 0, "received fd is valid");

    /* The passed fd must reach the same file: read from offset 0. */
    char vbuf[18] = {0};
    MUST(lseek(passed, 0, SEEK_SET) == 0, "lseek passed fd");
    MUST(read(passed, vbuf, 18) == 18, "read via passed fd");
    MUST(memcmp(vbuf, "scm-rights-payload", 18) == 0, "passed fd sees payload");

    close(passed); close(srcfd); close(sv[0]); close(sv[1]); unlink(tmpl);
    return 0;
fail:
    if (passed >= 0) close(passed);
    if (srcfd >= 0) { close(srcfd); unlink(tmpl); }
    if (sv[0] >= 0) close(sv[0]);
    if (sv[1] >= 0) close(sv[1]);
    return -1;
}

/* Named SOCK_DGRAM: bind a server, sendto() by path, recvfrom(). */
TEST(dgram_named)
{
    struct sockaddr_un a; mkpath(&a, "dgram");
    unlink(a.sun_path);
    int srv = socket(AF_UNIX, SOCK_DGRAM, 0);
    int cli = socket(AF_UNIX, SOCK_DGRAM, 0);
    MUST(srv >= 0 && cli >= 0, "socket x2 (dgram)");
    MUST(bind(srv, (struct sockaddr *)&a, sizeof(a)) == 0, "bind dgram server");

    const char *msg = "datagram";
    MUST(sendto(cli, msg, 8, 0, (struct sockaddr *)&a, sizeof(a)) == 8,
         "sendto by path");
    char buf[16] = {0};
    MUST(recvfrom(srv, buf, sizeof(buf), 0, NULL, NULL) == 8, "recvfrom");
    MUST(memcmp(buf, msg, 8) == 0, "datagram payload intact");

    close(srv); close(cli); unlink(a.sun_path);
    return 0;
fail:
    close(srv); close(cli); unlink(a.sun_path);
    return -1;
}

/* Reproduce tdeinit's exact dance: a 0700 socket directory, a socket
 * bound inside it, chmod the socket to 0600, then re-stat and verify
 * both the type and the owner/mode (tdeinit refuses to proceed unless
 * the socket is owned by the user and is mode 0600). */
TEST(tdeinit_socket_pattern)
{
    int s = -1;
    char dir[48];
    snprintf(dir, sizeof(dir), "/tmp/tafx-%ld-sockdir", (long)getpid());
    char path[64];
    snprintf(path, sizeof(path), "%s/tdeinit__0", dir);

    /* clean slate */
    unlink(path); rmdir(dir);
    MUST(mkdir(dir, 0700) == 0, "mkdir 0700 socket dir");

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(s >= 0, "socket");
    struct sockaddr_un a; memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof(a.sun_path), "%s", path);
    MUST(bind(s, (struct sockaddr *)&a, sizeof(a)) == 0, "bind in 0700 dir");
    MUST(listen(s, 5) == 0, "listen");

    /* tdeinit: chmod(sock, 0600), then stat and check ownership+mode. */
    MUST(chmod(path, 0600) == 0, "chmod socket 0600");
    struct stat st;
    MUST(stat(path, &st) == 0, "stat socket");
    MUST(S_ISSOCK(st.st_mode), "still a socket after chmod");
    MUST((st.st_mode & 0777) == 0600, "mode is exactly 0600");
    MUST(st.st_uid == getuid(), "socket owned by caller");

    close(s); unlink(path); rmdir(dir);
    return 0;
fail:
    close(s); unlink(path); rmdir(dir);
    return -1;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    int tests_run = 0, tests_pass = 0, tests_fail = 0;

    fprintf(stdout, "torture_afunix: AF_UNIX filesystem-node + permission battery\n");
    fprintf(stdout, "------------------------------------------------------------\n");

    RUN(bind_creates_fs_node);
    RUN(fstat_socket_fd);
    RUN(chmod_socket_path);
    RUN(fchmod_socket_fd);
    RUN(access_socket_path);
    RUN(bind_eaddrinuse);
    RUN(connect_enoent);
    RUN(connect_no_listener);
    RUN(connect_not_a_socket);
    RUN(unlink_rebind);
    RUN(scm_rights_fd_passing);
    RUN(dgram_named);
    RUN(tdeinit_socket_pattern);

    fprintf(stdout, "------------------------------------------------------------\n");
    fprintf(stdout, "Result: %d/%d passed", tests_pass, tests_run);
    if (tests_fail) fprintf(stdout, " (%d FAILED)", tests_fail);
    fprintf(stdout, "\n");

    return tests_fail == 0 ? 0 : 1;
}
