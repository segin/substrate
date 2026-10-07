/*
 * host_test_svr4_streams.c - the STREAMS transport shim
 * (sys/exec/perso/svr4/svr4_streams.c) against stub sockets.
 *
 * What a System V program does to reach a server is replayed call by call
 * -- the sequences are the ones Dell UNIX's libsocket and INTERACTIVE
 * UNIX's X library were traced making -- and what the shim did with the
 * socket underneath is checked.  Built -m32: the shim takes user addresses
 * as 32-bit numbers.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/perso/svr4/svr4_streams.h>
#include <exec/perso/sysv386.h>
#include <netinet/in.h>
#include <pm/pm.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/kern_syscalls.h>
#include <sys/lock.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/syscall_impl.h>
#include <vm/vm_kmem.h>

/* ---- the kernel underneath ------------------------------------------- */

static process_t the_process;
process_t *current_process = &the_process;

static file_t files[MAX_FD];
static char nodes[MAX_FD];              /* a socket's node: any address */
static int fd_family[MAX_FD];
static int fd_flags[MAX_FD];

static int connect_fd = -1;
static int connect_result;
static int connect_flags;               /* the descriptor's, when called */
static struct sockaddr_un connect_addr;
static socklen_t connect_len;
static int shutdown_how = -1;
static char written[64];
static size_t written_len;
static const char *to_read;

int cmdline_debug_enabled(const char *what) { (void)what; return 0; }
void kprint(const char *s) { (void)s; }
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void *kmalloc(size_t size) { return calloc(1, size); }
void kfree(void *p, size_t size) { (void)size; free(p); }
int copyin(const void *s, void *d, size_t n) { memcpy(d, s, n); return 0; }
int copyout(const void *s, void *d, size_t n) { memcpy(d, s, n); return 0; }
int sysv386_span(uint32_t addr, uint32_t len) { (void)addr; (void)len; return 0; }

int sys_socket(int family, int type, int protocol) {
    int fd;

    (void)type; (void)protocol;
    for (fd = 3; fd < MAX_FD; fd++) {
        if (!the_process.fds[fd]) {
            memset(&files[fd], 0, sizeof(files[fd]));
            files[fd].f_count = 1;
            files[fd].f_data = &nodes[fd];
            the_process.fds[fd] = &files[fd];
            fd_family[fd] = family;
            fd_flags[fd] = O_RDWR;
            return fd;
        }
    }
    return -EMFILE;
}

int sys_close(int fd) {
    the_process.fds[fd] = NULL;
    return 0;
}

int sys_fcntl(int fd, int cmd, int arg) {
    if (cmd == F_GETFL) {
        return fd_flags[fd];
    }
    if (cmd == F_SETFL) {
        fd_flags[fd] = (fd_flags[fd] & 3) | (arg & ~3);
    }
    return 0;
}

int kern_connect(int fd, const struct sockaddr *addr, socklen_t len) {
    connect_fd = fd;
    connect_flags = fd_flags[fd];
    connect_len = len;
    memset(&connect_addr, 0, sizeof(connect_addr));
    memcpy(&connect_addr, addr, len < sizeof(connect_addr) ? len
                                                           : sizeof(connect_addr));
    return connect_result;
}

int kern_sockname(int fd, int peer, void *kaddr, socklen_t *len) {
    struct sockaddr_in in;

    (void)fd;
    memset(&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_port = peer ? 0x7017 : 0x3412;
    memcpy(kaddr, &in, sizeof(in));
    *len = sizeof(in);
    return 0;
}

int sys_shutdown(int fd, int how) { (void)fd; shutdown_how = how; return 0; }

ssize_t kern_write(int fd, const char *buf, size_t len) {
    (void)fd;
    assert(written_len + len <= sizeof(written));
    memcpy(written + written_len, buf, len);
    written_len += len;
    return (ssize_t)len;
}

ssize_t kern_read(int fd, char *buf, size_t len) {
    size_t n;

    (void)fd;
    if (!to_read) {
        return -EAGAIN;
    }
    n = strlen(to_read);
    if (n > len) {
        n = len;
    }
    memcpy(buf, to_read, n);
    to_read = NULL;
    return (ssize_t)n;
}

int kern_ioctl(int fd, uint32_t request, void *arg) {
    (void)fd; (void)request;
    *(int32_t *)arg = to_read ? (int32_t)strlen(to_read) : 0;
    return 0;
}

int kern_poll(struct pollfd *fds, unsigned int nfds, int timeout) {
    unsigned int i;
    int ready = 0;

    (void)timeout;
    for (i = 0; i < nfds; i++) {
        fds[i].revents = (short)(fds[i].events & POLLOUT);
        ready += fds[i].revents != 0;
    }
    return ready;
}

/* ---- calling the shim ------------------------------------------------ */

static uint32_t U(const void *p) { return (uint32_t)(uintptr_t)p; }

static int open_dev(const char *path) {
    int64_t result = -1;

    assert(svr4_streams_open(path, O_RDWR, &result) == 1);
    return (int)result;
}

static int64_t ioctl(int fd, uint32_t request, uint32_t arg) {
    struct sysv386_frame f;
    int64_t result = -12345;

    memset(&f, 0, sizeof(f));
    f.a[0] = (uint32_t)fd;
    f.a[1] = request;
    f.a[2] = arg;
    assert(svr4_streams_ioctl(&f, &result) == 1);
    return result;
}

static int64_t str(int fd, int32_t cmd, void *buf, int32_t *len) {
    struct svr4_strioctl ioc = { cmd, 0, *len, U(buf) };
    int64_t rc = ioctl(fd, SVR4_I_STR, U(&ioc));

    *len = ioc.ic_len;
    return rc;
}

static int64_t putmsg(int fd, void *ctl, int32_t ctl_len, void *data,
                      int32_t data_len) {
    struct svr4_strbuf c = { 0, ctl_len, U(ctl) };
    struct svr4_strbuf d = { 0, data_len, U(data) };
    struct sysv386_frame f;

    memset(&f, 0, sizeof(f));
    f.a[0] = (uint32_t)fd;
    f.a[1] = ctl ? U(&c) : 0;
    f.a[2] = data ? U(&d) : 0;
    return svr4_sys_putmsg(&f);
}

static int64_t getmsg(int fd, struct svr4_strbuf *c, struct svr4_strbuf *d,
                      int32_t *flags) {
    struct sysv386_frame f;

    memset(&f, 0, sizeof(f));
    f.a[0] = (uint32_t)fd;
    f.a[1] = c ? U(c) : 0;
    f.a[2] = d ? U(d) : 0;
    f.a[3] = U(flags);
    return svr4_sys_getmsg(&f);
}

static void close_dev(int fd) {
    svr4_streams_close(fd);
    sys_close(fd);
}

/* ---- the tests ------------------------------------------------------- */

/* What is not a device is the filesystem's, and a descriptor that is not
 * a stream is not the shim's to answer for. */
static void test_not_ours(void) {
    struct sysv386_frame f;
    int64_t result = 0;

    assert(svr4_streams_open("/etc/passwd", O_RDONLY, &result) == 0);
    assert(svr4_streams_open("/dev/tcpx", O_RDWR, &result) == 0);
    assert(svr4_streams_open("/dev/X0S", O_RDWR, &result) == 0);
    assert(svr4_streams_open("/dev/X/server.", O_RDWR, &result) == 0);
    memset(&f, 0, sizeof(f));
    f.a[0] = 1;
    f.a[1] = SVR4_I_PUSH;
    assert(svr4_streams_ioctl(&f, &result) == 0);
    assert(svr4_streams_read(1, 0, 0, &result) == 0);
    f.a[0] = 1;
    assert(svr4_sys_putmsg(&f) == -EBADF);
}

static void test_modules(void) {
    int fd = open_dev(SVR4_DEV_TCP);
    char name[SVR4_FMNAMESZ + 1];

    assert(fd >= 3 && fd_family[fd] == AF_INET);
    assert(ioctl(fd, SVR4_I_FIND, U("sockmod")) == 0);
    assert(ioctl(fd, SVR4_I_LOOK, U(name)) == -EINVAL);
    assert(ioctl(fd, SVR4_I_PUSH, U("sockmod")) == 0);
    assert(ioctl(fd, SVR4_I_PUSH, U("nosuch")) == -EINVAL);
    assert(ioctl(fd, SVR4_I_FIND, U("sockmod")) == 1);
    assert(ioctl(fd, SVR4_I_FIND, U("timod")) == 0);
    assert(ioctl(fd, SVR4_I_LOOK, U(name)) == 0 && !strcmp(name, "sockmod"));
    assert(ioctl(fd, SVR4_I_POP, 0) == 0);
    assert(ioctl(fd, SVR4_I_POP, 0) == -EINVAL);
    close_dev(fd);
    /* Closed, it is a stream no longer. */
    {
        struct sysv386_frame f;
        int64_t result;

        memset(&f, 0, sizeof(f));
        f.a[0] = (uint32_t)fd;
        f.a[1] = SVR4_I_POP;
        assert(svr4_streams_ioctl(&f, &result) == 0);
    }
}

/* libsocket's socket(): SI_GETUDATA into a 28-byte structure.  An answer
 * of 32 bytes ran over the end of it and the program's stack. */
static void test_udata_fits(void) {
    int fd = open_dev(SVR4_DEV_TICOTSORD);
    int32_t buf[9];
    int32_t len = 28;

    memset(buf, 0x55, sizeof(buf));
    assert(str(fd, SVR4_SI_GETUDATA, buf, &len) == 0);
    assert(len == 28);
    assert(buf[7] == 0x55555555);               /* untouched */
    assert(buf[1] == (int32_t)sizeof(struct sockaddr_un));
    assert(buf[4] == SVR4_T_COTS_ORD);
    len = 32;
    assert(str(fd, SVR4_SI_GETUDATA, buf, &len) == 0 && len == 32);
    len = 8;
    assert(str(fd, SVR4_SI_GETUDATA, buf, &len) == -EINVAL);
    close_dev(fd);
}

/* connect() to a local socket, as libsocket does it: a bind to nothing by
 * Release 4's TI_BIND, then T_CONN_REQ with a sockaddr_un and what stat(2)
 * said of the file after it, then the two messages read back. */
static void test_connect_local(void) {
    int fd = open_dev(SVR4_DEV_TICOTSORD);
    uint8_t req[160], ctl[160];
    struct svr4_strbuf c = { sizeof(ctl), 0, U(ctl) };
    struct sockaddr_un un;
    int32_t w[5], len, flags;

    memset(req, 0, sizeof(req));
    w[0] = SVR4_T_BIND_REQ; w[1] = 124; w[2] = 16; w[3] = 0;
    memcpy(req, w, 16);
    req[16] = AF_UNIX;
    len = 140;
    assert(str(fd, SVR4_TI_BIND, req, &len) == 0);
    assert(len == 140);
    memcpy(w, req, 16);
    assert(w[0] == SVR4_T_BIND_ACK && w[1] == 124 && w[2] == 16);

    memset(req, 0, sizeof(req));
    w[0] = SVR4_T_CONN_REQ; w[1] = 124; w[2] = 20; w[3] = 0; w[4] = 0;
    memcpy(req, w, 20);
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strcpy(un.sun_path, "/tmp/.X11-unix/X0");
    memcpy(req + 20, &un, sizeof(un));
    w[0] = 8; w[1] = 0x1234; w[2] = 0x5678;     /* size, st_dev, st_ino */
    memcpy(req + 20 + 112, w, 12);

    fd_flags[fd] |= O_NONBLOCK;
    connect_result = 0;
    assert(putmsg(fd, req, 144, NULL, 0) == 0);
    assert(connect_fd == fd);
    assert(connect_addr.sun_family == AF_UNIX);
    assert(!strcmp(connect_addr.sun_path, "/tmp/.X11-unix/X0"));
    assert(connect_len == 2 + strlen("/tmp/.X11-unix/X0"));
    /* The provider waits for the connection, whatever the mode... */
    assert(!(connect_flags & O_NONBLOCK));
    /* ...and the mode is the program's again afterwards. */
    assert(fd_flags[fd] & O_NONBLOCK);

    /* The acknowledgement is a priority message; the confirmation is not,
     * and is not given to a caller asking for priority messages only. */
    flags = SVR4_RS_HIPRI;
    assert(getmsg(fd, &c, NULL, &flags) == 0);
    assert(c.len == 8 && flags == SVR4_RS_HIPRI);
    memcpy(w, ctl, 8);
    assert(w[0] == SVR4_T_OK_ACK && w[1] == SVR4_T_CONN_REQ);
    flags = SVR4_RS_HIPRI;
    assert(getmsg(fd, &c, NULL, &flags) == -EAGAIN);
    flags = 0;
    assert(getmsg(fd, &c, NULL, &flags) == 0);
    assert(c.len == 144 && flags == 0);
    memcpy(w, ctl, 20);
    assert(w[0] == SVR4_T_CONN_CON && w[1] == 124 && w[2] == 20);
    assert(!memcmp(ctl + 20, &un, sizeof(un)));

    /* Connected: a second request is out of state. */
    assert(putmsg(fd, req, 144, NULL, 0) == 0);
    flags = 0;
    assert(getmsg(fd, &c, NULL, &flags) == 0);
    memcpy(w, ctl, 16);
    assert(w[0] == SVR4_T_ERROR_ACK && w[2] == SVR4_TOUTSTATE);
    close_dev(fd);
}

/* A refused connection is acknowledged and then disconnected, with the
 * reason as Release 4 numbers it; and Release 3's TI_ numbers are timod's
 * too. */
static void test_connect_refused(void) {
    int fd = open_dev(SVR4_DEV_TCP);
    uint8_t req[64], ctl[64];
    struct svr4_strbuf c = { sizeof(ctl), 0, U(ctl) };
    struct sockaddr_in in;
    int32_t w[10], len, flags = 0;

    len = 4;
    w[0] = SVR4_T_INFO_REQ;
    assert(str(fd, SVR3_TI_GETINFO, w, &len) == 0 && len == 40);
    assert(w[0] == SVR4_T_INFO_ACK && w[5] == 16 && w[8] == SVR4_T_COTS_ORD);
    assert(w[9] == SVR4_TS_UNBND);

    memset(req, 0, sizeof(req));
    w[0] = SVR4_T_CONN_REQ; w[1] = 16; w[2] = 20; w[3] = 0; w[4] = 0;
    memcpy(req, w, 20);
    memset(&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_port = 0x7017;                       /* 6000, network order */
    in.sin_addr.s_addr = 0x0100007f;
    memcpy(req + 20, &in, sizeof(in));
    connect_result = -ECONNREFUSED;
    assert(putmsg(fd, req, 36, NULL, 0) == 0);
    assert(!memcmp(&connect_addr, &in, sizeof(in)) && connect_len == 16);
    assert(getmsg(fd, &c, NULL, &flags) == 0);
    memcpy(w, ctl, 8);
    assert(w[0] == SVR4_T_OK_ACK);
    flags = 0;                                  /* it came back RS_HIPRI */
    assert(getmsg(fd, &c, NULL, &flags) == 0);
    memcpy(w, ctl, 12);
    assert(c.len == 12 && w[0] == SVR4_T_DISCON_IND);
    assert(w[1] == SVR4_ECONNREFUSED);

    /* A bad address is the provider's refusal, which timod hands back as
     * the ioctl's value and putmsg as an error acknowledgement. */
    w[0] = SVR4_T_CONN_REQ; w[1] = 2; w[2] = 20; w[3] = 0; w[4] = 0;
    memcpy(req, w, 20);
    assert(putmsg(fd, req, 22, NULL, 0) == 0);
    assert(getmsg(fd, &c, NULL, &flags) == 0);
    memcpy(w, ctl, 16);
    assert(w[0] == SVR4_T_ERROR_ACK && w[2] == SVR4_TBADADDR);
    connect_result = 0;
    close_dev(fd);
}

/* Data, the release at the other end closing, and the names. */
static void test_data(void) {
    int fd = open_dev(SVR4_DEV_TCP);
    char data[16];
    uint8_t ctl[16], addr[16];
    struct svr4_strbuf c = { sizeof(ctl), 0, U(ctl) };
    struct svr4_strbuf d = { sizeof(data), 0, U(data) };
    struct svr4_strbuf nb = { sizeof(addr), 0, U(addr) };
    struct svr4_iovec iov[2] = { { U("ab"), 2 }, { U("cde"), 3 } };
    struct sysv386_frame f;
    int32_t w[2], flags = 0, n = -1;
    int64_t result;

    written_len = 0;
    w[0] = SVR4_T_DATA_REQ; w[1] = 0;
    assert(putmsg(fd, w, 8, "xyz", 3) == 0);
    assert(putmsg(fd, NULL, 0, "!", 1) == 0);
    memset(&f, 0, sizeof(f));
    f.a[0] = (uint32_t)fd; f.a[1] = U(iov); f.a[2] = 2;
    assert(svr4_sys_writev(&f) == 5);
    assert(written_len == 9 && !memcmp(written, "xyz!abcde", 9));

    to_read = "reply";
    assert(ioctl(fd, SVR4_I_NREAD, U(&n)) == 1 && n == 5);
    assert(ioctl(fd, SVR4_FIONREAD, U(&n)) == 0 && n == 5);
    assert(getmsg(fd, &c, &d, &flags) == 0);
    assert(c.len == -1 && d.len == 5 && !memcmp(data, "reply", 5));

    /* Nothing to read is EAGAIN on a stream, not the 0 a pipe gives. */
    assert(svr4_streams_read(fd, U(data), sizeof(data), &result) == 1);
    assert(result == -EAGAIN);

    /* The other end closes: an orderly release, once, and then nothing. */
    to_read = "";
    assert(getmsg(fd, &c, &d, &flags) == 0);
    memcpy(w, ctl, 4);
    assert(c.len == 4 && w[0] == SVR4_T_ORDREL_IND);
    to_read = "";
    assert(getmsg(fd, &c, &d, &flags) == 0);
    assert(c.len == -1 && d.len == 0);

    assert(ioctl(fd, SVR4_TI_GETPEERNAME, U(&nb)) == 0 && nb.len == 16);
    assert(addr[2] == 0x17 && addr[3] == 0x70);

    w[0] = SVR4_T_ORDREL_REQ;
    assert(putmsg(fd, w, 4, NULL, 0) == 0 && shutdown_how == SHUT_WR);
    close_dev(fd);
}

/* poll(2): a stream with an answer waiting is readable, and POLLRDNORM
 * is answered as it was asked. */
static void test_poll(void) {
    int fd = open_dev(SVR4_DEV_TCP);
    struct pollfd p = { fd, POLLIN | SVR4_POLLRDNORM | POLLOUT, 0 };
    struct sysv386_frame f;
    int32_t w = SVR4_T_INFO_REQ;

    memset(&f, 0, sizeof(f));
    f.a[0] = U(&p); f.a[1] = 1; f.a[2] = 0;
    assert(svr4_sys_poll(&f) == 1);
    assert(p.revents == POLLOUT);
    assert(p.events == (short)(POLLIN | SVR4_POLLRDNORM | POLLOUT));

    assert(putmsg(fd, &w, 4, NULL, 0) == 0);    /* queues a T_INFO_ACK */
    assert(svr4_sys_poll(&f) == 1);
    assert(p.revents == (short)(POLLIN | SVR4_POLLRDNORM | POLLOUT));
    f.a[1] = 5000;
    assert(svr4_sys_poll(&f) == -EINVAL);
    close_dev(fd);
}

/* The X transports. */
static void test_x(void) {
    struct svr4_strfdinsert ins;
    int a, b, req, fd;

    /* Release 4: the named stream is the connection. */
    connect_result = 0;
    fd = open_dev("/dev/X/server.3");
    assert(fd >= 3 && connect_fd == fd);
    assert(!strcmp(connect_addr.sun_path, "/tmp/.X11-unix/X3"));
    close_dev(fd);
    connect_result = -ECONNREFUSED;
    assert(open_dev("/dev/X/server.3") == -ENXIO);
    connect_result = 0;

    /* INTERACTIVE: two ends joined, one sent to the server through the
     * stream at its socket's name, the other kept and connected. */
    a = open_dev(SVR4_DEV_SPX);
    b = open_dev(SVR4_DEV_SPX);
    memset(&ins, 0, sizeof(ins));
    ins.fildes = b;
    connect_fd = -1;
    assert(ioctl(a, SVR4_I_FDINSERT, U(&ins)) == 0);
    assert(connect_fd == -1);
    req = open_dev("/tmp/.X11-unix/X0");
    assert(req >= 3);
    assert(ioctl(req, SVR4_I_SENDFD, (uint32_t)a) == 0);
    assert(connect_fd == b);
    assert(!strcmp(connect_addr.sun_path, "/tmp/.X11-unix/X0"));
    assert(ioctl(req, SVR4_I_SENDFD, (uint32_t)req) == -EINVAL);
    close_dev(req); close_dev(a); close_dev(b);

    /* SCO: one end, taken by the request stream. */
    req = open_dev("/dev/X12R");
    a = open_dev(SVR4_DEV_SPX);
    ins.fildes = a;
    assert(ioctl(req, SVR4_I_FDINSERT, U(&ins)) == 0);
    assert(connect_fd == a);
    assert(!strcmp(connect_addr.sun_path, "/tmp/.X11-unix/X12"));
    ins.fildes = req;
    assert(ioctl(req, SVR4_I_FDINSERT, U(&ins)) == -EINVAL);
    close_dev(req); close_dev(a);
}

static void test_errno(void) {
    assert(svr4_net_errno(ECONNREFUSED) == 146);
    assert(svr4_net_errno(ENOTCONN) == 134);
    assert(svr4_net_errno(ETIMEDOUT) == 145);
    assert(svr4_net_errno(ENOENT) == ENOENT);
}

int main(void) {
    test_not_ours();
    test_modules();
    test_udata_fits();
    test_connect_local();
    test_connect_refused();
    test_data();
    test_poll();
    test_x();
    test_errno();
    printf("host_test_svr4_streams: PASS\n");
    return 0;
}
