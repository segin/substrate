/*
 * torture_unix.c — AF_UNIX SOCK_STREAM test battery.
 *
 * Portable POSIX C: links against the host pthreads + libc when
 * built with the host toolchain; against substrate's libpthread +
 * libc when built with CROSS=/opt/substrate/bin/i386-unknown-substrate-.
 *
 * Each test prints "PASS" or "FAIL: <reason>" and exits the per-test
 * routine.  main() runs them in order and reports an aggregate at
 * the end; the program exits 0 iff every test passed.
 *
 * Scenarios:
 *   1. socketpair_roundtrip — write on one half, read on the other.
 *   2. socketpair_bidi      — both directions independently.
 *   3. named_listen_accept  — bind / listen / connect / accept.
 *   4. send_recv_flags      — send() / recv() (the flagless 4-arg form).
 *   5. sendto_recvfrom      — same but via the address-aware syscalls.
 *   6. sendmsg_recvmsg      — scatter/gather via struct msghdr + iovec.
 *   7. shutdown_read        — shutdown(SHUT_RD) blocks further reads.
 *   8. shutdown_write       — shutdown(SHUT_WR) signals EOF to peer.
 *   9. getsockname_getpeername — addresses round-trip through both calls.
 *  10. blocking_rendezvous  — thread waits in recv() until peer writes.
 *  11. large_payload        — push 64KiB across to verify buffer cycling.
 *  12. eof_on_close         — read returns 0 after peer close.
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MUST(cond, msg) do {                                          \
    if (!(cond)) {                                                    \
        fprintf(stderr, "  FAIL: %s (errno=%d: %s)\n",                \
                (msg), errno, strerror(errno));                       \
        return -1;                                                    \
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

static const int TOTAL = 12;

/* ------------------------------------------------------------------ */

TEST(socketpair_roundtrip)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    const char msg[] = "hello-sockets";
    ssize_t n = write(sv[0], msg, sizeof(msg));
    MUST(n == (ssize_t)sizeof(msg), "write half 0->1");

    char buf[64] = {0};
    n = read(sv[1], buf, sizeof(buf));
    MUST(n == (ssize_t)sizeof(msg), "read half 1");
    MUST(memcmp(buf, msg, sizeof(msg)) == 0, "payload mismatch");

    close(sv[0]); close(sv[1]);
    return 0;
}

TEST(socketpair_bidi)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    MUST(write(sv[0], "AB", 2) == 2, "write 0->1");
    MUST(write(sv[1], "CD", 2) == 2, "write 1->0");

    char a[2] = {0}, b[2] = {0};
    MUST(read(sv[1], a, 2) == 2, "read on 1");
    MUST(read(sv[0], b, 2) == 2, "read on 0");
    MUST(a[0] == 'A' && a[1] == 'B', "1 saw 0's data");
    MUST(b[0] == 'C' && b[1] == 'D', "0 saw 1's data");

    close(sv[0]); close(sv[1]);
    return 0;
}

/* Helper: build a sockaddr_un for the named-listen test.  Use the
 * pid to avoid collision when several copies run concurrently. */
static void make_path(struct sockaddr_un *sun, const char *tag)
{
    memset(sun, 0, sizeof(*sun));
    sun->sun_family = AF_UNIX;
    snprintf(sun->sun_path, sizeof(sun->sun_path),
             "/tmp/torture_unix.%d.%s", (int)getpid(), tag);
}

TEST(named_listen_accept)
{
    struct sockaddr_un addr;
    make_path(&addr, "named");
    unlink(addr.sun_path);

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(srv >= 0, "socket(server)");
    MUST(bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0, "bind");
    MUST(listen(srv, 4) == 0, "listen");

    int cli = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(cli >= 0, "socket(client)");
    MUST(connect(cli, (struct sockaddr *)&addr, sizeof(addr)) == 0, "connect");

    int s = accept(srv, NULL, NULL);
    MUST(s >= 0, "accept");

    MUST(write(cli, "ping", 4) == 4, "client write");
    char buf[8] = {0};
    MUST(read(s, buf, 4) == 4, "server read");
    MUST(memcmp(buf, "ping", 4) == 0, "payload");

    close(s); close(cli); close(srv);
    unlink(addr.sun_path);
    return 0;
}

TEST(send_recv_flags)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    const char msg[] = "via-send";
    MUST(send(sv[0], msg, sizeof(msg), 0) == (ssize_t)sizeof(msg), "send");
    char buf[32] = {0};
    MUST(recv(sv[1], buf, sizeof(buf), 0) == (ssize_t)sizeof(msg), "recv");
    MUST(memcmp(buf, msg, sizeof(msg)) == 0, "payload");

    close(sv[0]); close(sv[1]);
    return 0;
}

TEST(sendto_recvfrom)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    const char msg[] = "via-sendto";
    /* On a connected SOCK_STREAM, sendto() with NULL dest = send. */
    MUST(sendto(sv[0], msg, sizeof(msg), 0, NULL, 0) == (ssize_t)sizeof(msg),
         "sendto");

    char buf[32] = {0};
    socklen_t fromlen = 0;
    MUST(recvfrom(sv[1], buf, sizeof(buf), 0, NULL, &fromlen) ==
         (ssize_t)sizeof(msg), "recvfrom");
    MUST(memcmp(buf, msg, sizeof(msg)) == 0, "payload");

    close(sv[0]); close(sv[1]);
    return 0;
}

TEST(sendmsg_recvmsg)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    struct iovec iov[2];
    iov[0].iov_base = (void *)"piece1-";
    iov[0].iov_len  = 7;
    iov[1].iov_base = (void *)"piece2";
    iov[1].iov_len  = 6;

    struct msghdr mh;
    memset(&mh, 0, sizeof(mh));
    mh.msg_iov = iov;
    mh.msg_iovlen = 2;
    MUST(sendmsg(sv[0], &mh, 0) == 13, "sendmsg");

    char buf[32] = {0};
    struct iovec riov = { buf, sizeof(buf) };
    struct msghdr rmh;
    memset(&rmh, 0, sizeof(rmh));
    rmh.msg_iov = &riov;
    rmh.msg_iovlen = 1;
    MUST(recvmsg(sv[1], &rmh, 0) == 13, "recvmsg");
    MUST(memcmp(buf, "piece1-piece2", 13) == 0, "payload");

    close(sv[0]); close(sv[1]);
    return 0;
}

TEST(shutdown_read)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    MUST(shutdown(sv[1], SHUT_RD) == 0, "shutdown SHUT_RD");
    /* After shutting our own read side, reads should return 0 (EOF)
     * rather than blocking.  Some implementations return -EPIPE on
     * an SO_SNDBUF-full write; we don't probe that here. */
    char buf[4];
    ssize_t n = read(sv[1], buf, sizeof(buf));
    MUST(n == 0, "read after SHUT_RD returns EOF");

    close(sv[0]); close(sv[1]);
    return 0;
}

TEST(shutdown_write)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    MUST(write(sv[0], "tail", 4) == 4, "write before shutdown");
    MUST(shutdown(sv[0], SHUT_WR) == 0, "shutdown SHUT_WR");

    char buf[16];
    MUST(read(sv[1], buf, sizeof(buf)) == 4, "read residual");
    /* After peer shut their write side, our next read should see EOF. */
    MUST(read(sv[1], buf, sizeof(buf)) == 0, "EOF after peer shutdown");

    close(sv[0]); close(sv[1]);
    return 0;
}

TEST(getsockname_getpeername)
{
    struct sockaddr_un srv_addr;
    make_path(&srv_addr, "naming");
    unlink(srv_addr.sun_path);

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(srv >= 0, "socket(server)");
    MUST(bind(srv, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) == 0, "bind");
    MUST(listen(srv, 4) == 0, "listen");

    int cli = socket(AF_UNIX, SOCK_STREAM, 0);
    MUST(cli >= 0, "socket(client)");
    MUST(connect(cli, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) == 0, "connect");

    int peer = accept(srv, NULL, NULL);
    MUST(peer >= 0, "accept");

    /* Client's view of its peer = server's bound path. */
    struct sockaddr_un got;
    socklen_t glen = sizeof(got);
    memset(&got, 0, sizeof(got));
    MUST(getpeername(cli, (struct sockaddr *)&got, &glen) == 0, "getpeername");
    MUST(got.sun_family == AF_UNIX, "peer family");
    MUST(strcmp(got.sun_path, srv_addr.sun_path) == 0, "peer path");

    /* Server-side accepted fd has a name too, even if anonymous (empty). */
    glen = sizeof(got);
    memset(&got, 0, sizeof(got));
    MUST(getsockname(peer, (struct sockaddr *)&got, &glen) == 0, "getsockname");
    MUST(got.sun_family == AF_UNIX, "self family");

    close(peer); close(cli); close(srv);
    unlink(srv_addr.sun_path);
    return 0;
}

/* ------------------------------------------------------------------ */

struct rendezvous_arg {
    int  fd;
    char buf[16];
    int  n;
};

static void *rendezvous_reader(void *p)
{
    struct rendezvous_arg *a = (struct rendezvous_arg *)p;
    a->n = (int)read(a->fd, a->buf, sizeof(a->buf));
    return NULL;
}

TEST(blocking_rendezvous)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    struct rendezvous_arg arg = { sv[1], {0}, 0 };
    pthread_t th;
    MUST(pthread_create(&th, NULL, rendezvous_reader, &arg) == 0, "pthread_create");

    /* Give the reader time to enter read(); a sleep is the simplest
     * portable rendezvous primitive that doesn't bake in libpthread
     * cond_t (substrate's libpthread cond support is still pending). */
    struct timespec ts = { 0, 100 * 1000 * 1000 };  /* 100 ms */
    nanosleep(&ts, NULL);

    MUST(write(sv[0], "wake!", 5) == 5, "write to wake reader");
    pthread_join(th, NULL);

    MUST(arg.n == 5, "reader saw 5 bytes");
    MUST(memcmp(arg.buf, "wake!", 5) == 0, "payload");

    close(sv[0]); close(sv[1]);
    return 0;
}

#define LARGE_BYTES (64 * 1024)
struct large_writer_arg {
    int fd;
    const unsigned char *p;
    size_t n;
};

static void *large_writer_fn(void *vp)
{
    struct large_writer_arg *w = (struct large_writer_arg *)vp;
    size_t off = 0;
    while (off < w->n) {
        ssize_t k = write(w->fd, w->p + off, w->n - off);
        if (k <= 0) return (void *)1;
        off += (size_t)k;
    }
    return NULL;
}

TEST(large_payload)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    /* 64 KiB transfer.  Substrate's per-socket rx buffer is 4 KiB,
     * so this forces multiple producer/consumer cycles.  Use a
     * background writer + foreground reader so neither side has to
     * drain the whole stream before the other can act. */
    static unsigned char src[LARGE_BYTES];
    static unsigned char dst[LARGE_BYTES];
    for (size_t i = 0; i < LARGE_BYTES; i++) src[i] = (unsigned char)(i * 31 + 7);

    struct large_writer_arg warg = { sv[0], src, LARGE_BYTES };
    pthread_t th;
    MUST(pthread_create(&th, NULL, large_writer_fn, &warg) == 0, "writer thread");

    size_t off = 0;
    while (off < LARGE_BYTES) {
        ssize_t k = read(sv[1], dst + off, LARGE_BYTES - off);
        MUST(k > 0, "read returned <=0 mid-stream");
        off += (size_t)k;
    }
    void *rc = NULL;
    pthread_join(th, &rc);
    MUST(rc == NULL, "writer thread reported error");
    MUST(memcmp(src, dst, LARGE_BYTES) == 0, "payload integrity");

    close(sv[0]); close(sv[1]);
    return 0;
}

TEST(eof_on_close)
{
    int sv[2];
    MUST(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    close(sv[0]);
    char buf[4];
    ssize_t n = read(sv[1], buf, sizeof(buf));
    MUST(n == 0, "read returned EOF after peer close");

    close(sv[1]);
    return 0;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    int tests_run = 0, tests_pass = 0, tests_fail = 0;

    fprintf(stdout, "torture_unix: AF_UNIX SOCK_STREAM test battery\n");
    fprintf(stdout, "------------------------------------------------\n");

    RUN(socketpair_roundtrip);
    RUN(socketpair_bidi);
    RUN(named_listen_accept);
    RUN(send_recv_flags);
    RUN(sendto_recvfrom);
    RUN(sendmsg_recvmsg);
    RUN(shutdown_read);
    RUN(shutdown_write);
    RUN(getsockname_getpeername);
    RUN(blocking_rendezvous);
    RUN(large_payload);
    RUN(eof_on_close);

    fprintf(stdout, "------------------------------------------------\n");
    fprintf(stdout, "Result: %d/%d passed", tests_pass, tests_run);
    if (tests_fail) fprintf(stdout, " (%d FAILED)", tests_fail);
    fprintf(stdout, "\n");

    return tests_fail == 0 ? 0 : 1;
}
