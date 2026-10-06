/*
 * svr4_streams.c - the STREAMS transport interface, over sockets.
 *
 * A System V program does not reach the network with socket system calls;
 * there are none.  It opens a transport provider -- /dev/tcp, /dev/ticotsord
 * -- pushes a module on it, and talks to the provider in Transport Provider
 * Interface messages: putmsg(2) a T_CONN_REQ, getmsg(2) the T_OK_ACK and the
 * T_CONN_CON.  libnsl's t_connect() and libsocket's connect() are both
 * written that way, as is the X library's own transport code beneath them.
 * What goes over the wire once connected is what any system sends.
 *
 * So this is the provider and the modules, for the client's half: opening
 * one of the devices makes a socket, the messages are carried out with it
 * and answered as a provider answers them, and read(2) and write(2) on the
 * descriptor are the socket's own.  Enough of it is here for a program to
 * connect, exchange data and close; a server's half -- listening, and
 * T_CONN_IND/T_CONN_RES to accept -- is not.
 *
 * The local transports of the X Window System are here too, since they are
 * streams and nothing else speaks them: /dev/X/server.N, and /dev/XNR with
 * /dev/spx (see svr4_streams_open()).  Each ends up connected to substrate's
 * X server where it listens, /tmp/.X11-unix/XN.
 *
 * Layouts and numbers are those of <sys/stropts.h>, <sys/tihdr.h>,
 * <sys/timod.h> and <sys/sockmod.h> in UNIX System V/386 Release 4.0.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <exec/perso/svr4/svr4_streams.h>
#include <exec/perso/sysv386.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <netinet/in.h>
#include <pm/pm.h>
#include <sys/copy.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/kern_syscalls.h>
#include <sys/lock.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/syscall_impl.h>
#include <sys/termios.h>
#include <sys/un.h>
#include <vm/vm_kmem.h>

#define STREAMS_MAX     64      /* streams open at once, system-wide */
#define STREAM_MODS     4       /* modules pushed on one */
#define STREAM_MSGS     4       /* messages waiting to be read from one */
#define STREAM_CTL      192     /* the control part of the largest */
#define STREAM_TIDU     4096    /* what a provider says it takes at once */
#define STREAM_ADDR     128     /* the largest address */
#define STREAM_POLL_MAX 1024    /* descriptors in one poll(2) */
#define STREAM_IOV_MAX  16      /* vectors in one readv(2)/writev(2) */

enum stream_kind {
    STREAM_PROVIDER,            /* /dev/tcp and the like */
    STREAM_SPX,                 /* /dev/spx: half of a pipe to be */
    STREAM_XREQ,                /* /dev/XNR: where X takes requests */
};

struct stream_msg {
    int32_t len;
    int32_t hipri;              /* an acknowledgement: M_PCPROTO */
    uint8_t ctl[STREAM_CTL];
};

struct stream {
    const void *node;           /* the socket's node; NULL if free */
    uint8_t kind;
    uint8_t family;
    uint8_t type;
    uint8_t bound;
    uint8_t connected;
    uint8_t eof;                /* the release indication has been read */
    uint32_t display;           /* STREAM_XREQ: which server */
    const void *pair;           /* STREAM_SPX: the pipe's other end */
    uint32_t nmods;
    char mods[STREAM_MODS][SVR4_FMNAMESZ + 1];
    uint32_t qhead, qlen;
    struct stream_msg q[STREAM_MSGS];
};

static struct stream streams[STREAMS_MAX];
static mutex_t streams_lock;
static int streams_ready;

static int streams_trace(void) {
    return cmdline_debug_enabled("perso:svr4:streams");
}

static void streams_log(const char *what, int fd, const uint8_t *p,
                        uint32_t len) {
    char line[200];
    int n;
    uint32_t i;

    if (!streams_trace()) {
        return;
    }
    n = snprintf(line, sizeof(line), "STREAMS: [%d] fd %d %s (%u):",
                 current_process ? (int)current_process->pid : -1, fd, what,
                 (unsigned int)len);
    for (i = 0; i < len && n > 0 && (size_t)n + 4 < sizeof(line); i++) {
        n += snprintf(line + n, sizeof(line) - (size_t)n, " %02x", p[i]);
    }
    if (n > 0 && (size_t)n + 2 < sizeof(line)) {
        line[n++] = '\n';
        line[n] = '\0';
    }
    kprint(line);
}

static int user_put(uint32_t dst, const void *src, uint32_t len) {
    if (sysv386_span(dst, len) != 0 ||
        copyout(src, (void *)(uintptr_t)dst, len) != 0) {
        return -EFAULT;
    }
    return 0;
}

static int user_get(uint32_t src, void *dst, uint32_t len) {
    if (sysv386_span(src, len) != 0 ||
        copyin((const void *)(uintptr_t)src, dst, len) != 0) {
        return -EFAULT;
    }
    return 0;
}

int svr4_net_errno(int native) {
    switch (native) {
    case ENOTSOCK:      return SVR4_ENOTSOCK;
    case EPROTO:        return SVR4_EPROTO;
    case EAFNOSUPPORT:  return SVR4_EAFNOSUPPORT;
    case EADDRINUSE:    return SVR4_EADDRINUSE;
    case EADDRNOTAVAIL: return SVR4_EADDRNOTAVAIL;
    case ENETDOWN:      return SVR4_ENETDOWN;
    case ENETUNREACH:   return SVR4_ENETUNREACH;
    case ECONNABORTED:  return SVR4_ECONNABORTED;
    case ECONNRESET:    return SVR4_ECONNRESET;
    case ENOBUFS:       return SVR4_ENOBUFS;
    case EISCONN:       return SVR4_EISCONN;
    case ENOTCONN:      return SVR4_ENOTCONN;
    case ECONNREFUSED:  return SVR4_ECONNREFUSED;
    case EHOSTUNREACH:  return SVR4_EHOSTUNREACH;
    case EALREADY:      return SVR4_EALREADY;
    case EINPROGRESS:   return SVR4_EINPROGRESS;
    case ETIMEDOUT:     return 145;
    default:            return native;
    }
}

/* ---- the table ------------------------------------------------------- */

static void streams_init(void) {
    if (!streams_ready) {
        mutex_init(&streams_lock, "svr4_streams");
        streams_ready = 1;
    }
}

static const void *fd_node(int fd) {
    file_t *fp;

    if (!current_process || fd < 0 || fd >= MAX_FD) {
        return NULL;
    }
    fp = current_process->fds[fd];
    return fp ? fp->f_data : NULL;
}

/* The stream `fd` is, or NULL.  A stream is known by its socket's node, so
 * it is the same stream through a dup(2) or in a child. */
static struct stream *stream_of(int fd) {
    const void *node = fd_node(fd);
    struct stream *found = NULL;
    int i;

    if (!node || !streams_ready) {
        return NULL;
    }
    mutex_lock(&streams_lock);
    for (i = 0; i < STREAMS_MAX; i++) {
        if (streams[i].node == node) {
            found = &streams[i];
            break;
        }
    }
    mutex_unlock(&streams_lock);
    return found;
}

/* A socket of `family` and `type` on a new descriptor, and its stream.
 * A stale entry for a node that has since been freed and allocated again
 * is the one taken, so it never outlives the reuse. */
static int stream_create(int family, int type, int kind, int oflags,
                         struct stream **out) {
    int fd = sys_socket(family, type, 0);
    const void *node;
    struct stream *s = NULL;
    int i;

    if (fd < 0) {
        return fd;
    }
    node = fd_node(fd);
    streams_init();
    mutex_lock(&streams_lock);
    for (i = 0; i < STREAMS_MAX; i++) {
        if (streams[i].node == node) {
            s = &streams[i];
            break;
        }
    }
    for (i = 0; !s && i < STREAMS_MAX; i++) {
        if (!streams[i].node) {
            s = &streams[i];
        }
    }
    if (s) {
        memset(s, 0, sizeof(*s));
        s->node = node;
        s->kind = (uint8_t)kind;
        s->family = (uint8_t)family;
        s->type = (uint8_t)type;
    }
    mutex_unlock(&streams_lock);
    if (!s) {
        sys_close(fd);
        return -ENOSR;
    }
    if (oflags & O_NONBLOCK) {
        sys_fcntl(fd, F_SETFL, O_NONBLOCK);
    }
    *out = s;
    return fd;
}

void svr4_streams_close(int fd) {
    struct stream *s = stream_of(fd);
    file_t *fp;

    if (!s) {
        return;
    }
    fp = current_process->fds[fd];
    if (fp && fp->f_count <= 1) {
        mutex_lock(&streams_lock);
        s->node = NULL;
        mutex_unlock(&streams_lock);
    }
}

/*
 * read(2).  With nothing to read and no waiting asked for, a pipe or a
 * terminal returns 0 under O_NDELAY, and that is what the shared read
 * gives for anything.  A stream that is not a terminal fails with EAGAIN
 * instead, under O_NDELAY and O_NONBLOCK alike -- and has to here, because
 * on a connection 0 is the other end having closed, which is what the X
 * library takes it for.
 */
int svr4_streams_read(int fd, uint32_t buf, uint32_t len, int64_t *result) {
    if (!stream_of(fd)) {
        return 0;
    }
    if (sysv386_span(buf, len) != 0) {
        *result = -EFAULT;
        return 1;
    }
    *result = kern_read(fd, (char *)(uintptr_t)buf, (size_t)len);
    return 1;
}

/* ---- messages -------------------------------------------------------- */

static void put32(uint8_t *p, int32_t v) {
    memcpy(p, &v, sizeof(v));
}

static int32_t get32(const uint8_t *p) {
    int32_t v;

    memcpy(&v, p, sizeof(v));
    return v;
}

/* Queue `nwords` words and `tail` for getmsg(2). */
static void stream_reply(struct stream *s, int hipri, const int32_t *words,
                         uint32_t nwords, const uint8_t *tail,
                         uint32_t tail_len) {
    struct stream_msg *m;
    uint32_t i, len = nwords * 4U;

    if (s->qlen >= STREAM_MSGS) {
        return;
    }
    m = &s->q[(s->qhead + s->qlen) % STREAM_MSGS];
    for (i = 0; i < nwords; i++) {
        put32(m->ctl + i * 4U, words[i]);
    }
    if (tail_len > STREAM_CTL - len) {
        tail_len = STREAM_CTL - len;
    }
    if (tail_len) {
        memcpy(m->ctl + len, tail, tail_len);
    }
    m->len = (int32_t)(len + tail_len);
    m->hipri = hipri;
    s->qlen++;
}

static void reply_ok(struct stream *s, int32_t prim) {
    int32_t w[2] = { SVR4_T_OK_ACK, prim };

    stream_reply(s, 1, w, 2, NULL, 0);
}

static void reply_error(struct stream *s, int32_t prim, int32_t terr,
                        int native_errno) {
    int32_t w[4] = { SVR4_T_ERROR_ACK, prim, terr,
                     svr4_net_errno(native_errno) };

    stream_reply(s, 1, w, 4, NULL, 0);
}

/* ---- addresses ------------------------------------------------------- */

static uint32_t stream_addr_size(const struct stream *s) {
    return s->family == AF_INET ? (uint32_t)sizeof(struct sockaddr_in)
                                : (uint32_t)sizeof(struct sockaddr_un);
}

static int32_t stream_servtype(const struct stream *s) {
    return s->type == SOCK_DGRAM ? SVR4_T_CLTS : SVR4_T_COTS_ORD;
}

static int32_t stream_state(const struct stream *s) {
    return s->connected ? SVR4_TS_DATA_XFER
                        : s->bound ? SVR4_TS_IDLE : SVR4_TS_UNBND;
}

/*
 * The address a program gave, as one for a socket of the stream's family.
 * An Internet address is a sockaddr_in on both sides.  A local one is a
 * path, which libsocket hands over as a sockaddr_un; anything else on a
 * loopback provider is a name between System V programs that no socket
 * answers to.
 */
static int stream_native_addr(const struct stream *s, const uint8_t *addr,
                              uint32_t len, uint8_t *out, socklen_t *outlen) {
    memset(out, 0, STREAM_ADDR);
    if (s->family == AF_INET) {
        struct sockaddr_in in;

        if (len < 8) {
            return -EINVAL;
        }
        memset(&in, 0, sizeof(in));
        memcpy(&in, addr, len < sizeof(in) ? len : sizeof(in));
        in.sin_family = AF_INET;
        memcpy(out, &in, sizeof(in));
        *outlen = sizeof(in);
        return 0;
    } else {
        struct sockaddr_un un;
        uint32_t plen;

        if (len < 3 || addr[2] != '/') {
            return -EINVAL;
        }
        plen = len - 2;
        if (plen > sizeof(un.sun_path) - 1) {
            plen = sizeof(un.sun_path) - 1;
        }
        memset(&un, 0, sizeof(un));
        un.sun_family = AF_UNIX;
        memcpy(un.sun_path, addr + 2, plen);
        plen = (uint32_t)strlen(un.sun_path);
        memcpy(out, &un, 2 + plen + 1);
        *outlen = (socklen_t)(2 + plen);
        return 0;
    }
}

/* connect(2), waiting for it whatever the descriptor's mode: a provider
 * acknowledges a connection request before the program looks. */
static int stream_connect(int fd, const uint8_t *addr, socklen_t len) {
    int fl = sys_fcntl(fd, F_GETFL, 0);
    int rc;

    if (fl >= 0 && (fl & O_NONBLOCK)) {
        sys_fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    }
    rc = kern_connect(fd, (const struct sockaddr *)addr, len);
    if (fl >= 0 && (fl & O_NONBLOCK)) {
        sys_fcntl(fd, F_SETFL, fl);
    }
    return rc;
}

static int stream_connect_x(int fd, struct stream *s, uint32_t display) {
    struct sockaddr_un un;
    int rc;

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), SVR4_X_SOCKET_FMT,
             (unsigned int)display);
    rc = stream_connect(fd, (const uint8_t *)&un,
                        (socklen_t)(2 + strlen(un.sun_path)));
    if (rc == 0) {
        s->bound = 1;
        s->connected = 1;
    }
    return rc;
}

/* ---- the provider's answers ------------------------------------------ */

static void reply_info(struct stream *s, int hipri) {
    int32_t w[10];

    w[0] = SVR4_T_INFO_ACK;
    w[1] = s->type == SOCK_DGRAM ? STREAM_TIDU : 0;     /* TSDU: a stream */
    w[2] = s->family == AF_INET && s->type == SOCK_STREAM ? 1 : -2;
    w[3] = -2;                                          /* no connect data */
    w[4] = -2;
    w[5] = (int32_t)stream_addr_size(s);
    w[6] = STREAM_ADDR;
    w[7] = STREAM_TIDU;
    w[8] = stream_servtype(s);
    w[9] = stream_state(s);
    stream_reply(s, hipri, w, 10, NULL, 0);
}

/* T_BIND_REQ.  A client binds before it connects, to no address in
 * particular, and a socket does that for itself; the address acknowledged
 * is the one asked for, or a null one of the family. */
static void reply_bind(struct stream *s, const uint8_t *req, uint32_t len) {
    int32_t w[4] = { SVR4_T_BIND_ACK, 0, 16, 0 };
    uint8_t addr[STREAM_ADDR];
    uint32_t alen = 0, aoff;

    memset(addr, 0, sizeof(addr));
    if (len >= 16) {
        alen = (uint32_t)get32(req + 4);
        aoff = (uint32_t)get32(req + 8);
        w[3] = get32(req + 12);
        if (alen > sizeof(addr) || aoff > len || alen > len - aoff) {
            alen = 0;
        } else {
            memcpy(addr, req + aoff, alen);
        }
    }
    if (alen == 0) {
        struct sockaddr sa;

        memset(&sa, 0, sizeof(sa));
        sa.sa_family = s->family;
        memcpy(addr, &sa, sizeof(sa));
        alen = sizeof(sa);
    }
    w[1] = (int32_t)alen;
    s->bound = 1;
    stream_reply(s, 1, w, 4, addr, alen);
}

static void reply_optmgmt(struct stream *s, const uint8_t *req, uint32_t len) {
    struct stream_msg *m;

    if (s->qlen >= STREAM_MSGS || len < 16) {
        reply_error(s, SVR4_T_OPTMGMT_REQ, SVR4_TSYSERR, EINVAL);
        return;
    }
    /* The options asked for are the ones in force. */
    m = &s->q[(s->qhead + s->qlen) % STREAM_MSGS];
    if (len > STREAM_CTL) {
        len = STREAM_CTL;
    }
    memcpy(m->ctl, req, len);
    put32(m->ctl, SVR4_T_OPTMGMT_ACK);
    m->len = (int32_t)len;
    m->hipri = 1;
    s->qlen++;
}

/* T_CONN_REQ: the acknowledgement, and then either the confirmation or
 * the disconnect that says why not. */
static void request_connect(int fd, struct stream *s, const uint8_t *req,
                            uint32_t len) {
    uint8_t native[STREAM_ADDR];
    socklen_t nlen = 0;
    uint32_t alen, aoff;
    int rc;

    if (len < 20) {
        reply_error(s, SVR4_T_CONN_REQ, SVR4_TSYSERR, EINVAL);
        return;
    }
    alen = (uint32_t)get32(req + 4);
    aoff = (uint32_t)get32(req + 8);
    if (aoff > len || alen > len - aoff ||
        stream_native_addr(s, req + aoff, alen, native, &nlen) != 0) {
        reply_error(s, SVR4_T_CONN_REQ, SVR4_TBADADDR, 0);
        return;
    }
    if (s->connected) {
        reply_error(s, SVR4_T_CONN_REQ, SVR4_TOUTSTATE, 0);
        return;
    }
    rc = stream_connect(fd, native, nlen);
    reply_ok(s, SVR4_T_CONN_REQ);
    if (rc == 0) {
        int32_t w[5] = { SVR4_T_CONN_CON, (int32_t)alen, 20, 0, 0 };

        s->bound = 1;
        s->connected = 1;
        stream_reply(s, 0, w, 5, req + aoff, alen);
    } else {
        int reason = rc == -ENOENT ? ECONNREFUSED : -rc;
        int32_t w[3] = { SVR4_T_DISCON_IND, svr4_net_errno(reason), -1 };

        stream_reply(s, 0, w, 3, NULL, 0);
    }
}

/* One TPI request, from putmsg(2) or inside a timod ioctl. */
static int stream_request(int fd, struct stream *s, const uint8_t *req,
                          uint32_t len) {
    int32_t prim = get32(req);

    streams_log("request", fd, req, len);
    switch (prim) {
    case SVR4_T_CONN_REQ:
        request_connect(fd, s, req, len);
        return 0;
    case SVR4_T_BIND_REQ:
        reply_bind(s, req, len);
        return 0;
    case SVR4_T_UNBIND_REQ:
        s->bound = 0;
        reply_ok(s, prim);
        return 0;
    case SVR4_T_INFO_REQ:
        reply_info(s, 1);
        return 0;
    case SVR4_T_OPTMGMT_REQ:
        reply_optmgmt(s, req, len);
        return 0;
    case SVR4_T_DISCON_REQ:
        sys_shutdown(fd, SHUT_RDWR);
        s->connected = 0;
        reply_ok(s, prim);
        return 0;
    case SVR4_T_ORDREL_REQ:
        sys_shutdown(fd, SHUT_WR);
        return 0;
    default:
        reply_error(s, prim, SVR4_TNOTSUPPORT, 0);
        return 0;
    }
}

/* ---- open ------------------------------------------------------------ */

/* The digits at `p`, which must be followed by `end` and nothing more. */
static int parse_display(const char *p, const char *end, uint32_t *out) {
    uint32_t n = 0;

    if (*p < '0' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        n = n * 10U + (uint32_t)(*p++ - '0');
    }
    if (strcmp(p, end) != 0) {
        return 0;
    }
    *out = n;
    return 1;
}

/*
 * The devices.  The transport providers are a socket of the matching kind.
 * The X transports:
 *
 *   /dev/X/server.N   Release 4's: a named stream, which is a connection to
 *                     the server as soon as it is open.
 *   /dev/spx, with /dev/XNR or /tmp/.X11-unix/XN
 *                     Release 3's.  /dev/spx is one end of a stream pipe
 *                     with nothing at the other; the client gets its end
 *                     joined to the server by way of the server's request
 *                     stream, which is /dev/XNR on SCO's systems and a
 *                     stream mounted at /tmp/.X11-unix/XN on INTERACTIVE's.
 *                     Either it opens one /dev/spx and has the request
 *                     stream take it with I_FDINSERT, or it opens two,
 *                     joins them to each other with I_FDINSERT, and sends
 *                     one to the server with I_SENDFD, keeping the other.
 *                     Here /dev/spx is an unconnected socket, and the end
 *                     the client keeps is connected at that last step.
 *                     (Opening /tmp/.X11-unix/XN is otherwise an open of a
 *                     socket's file, which is no use to anything.)
 */
int svr4_streams_open(const char *path, int flags, int64_t *result) {
    static const struct {
        const char *path;
        int family, type, kind;
    } devices[] = {
        { SVR4_DEV_TCP,       AF_INET, SOCK_STREAM, STREAM_PROVIDER },
        { SVR4_DEV_UDP,       AF_INET, SOCK_DGRAM,  STREAM_PROVIDER },
        { SVR4_DEV_TICOTS,    AF_UNIX, SOCK_STREAM, STREAM_PROVIDER },
        { SVR4_DEV_TICOTSORD, AF_UNIX, SOCK_STREAM, STREAM_PROVIDER },
        { SVR4_DEV_TICLTS,    AF_UNIX, SOCK_DGRAM,  STREAM_PROVIDER },
        { SVR4_DEV_SPX,       AF_UNIX, SOCK_STREAM, STREAM_SPX },
    };
    struct stream *s = NULL;
    uint32_t display = 0;
    size_t i;
    int fd, rc;

    if (strncmp(path, "/dev/", 5) == 0) {
        streams_log(path, -1, NULL, 0);          /* which device, in a trace */
    }
    for (i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
        if (strcmp(path, devices[i].path) == 0) {
            *result = stream_create(devices[i].family, devices[i].type,
                                    devices[i].kind, flags, &s);
            return 1;
        }
    }
    if (strncmp(path, SVR4_DEV_X_SERVER, strlen(SVR4_DEV_X_SERVER)) == 0 &&
        parse_display(path + strlen(SVR4_DEV_X_SERVER), "", &display)) {
        fd = stream_create(AF_UNIX, SOCK_STREAM, STREAM_PROVIDER, 0, &s);
        if (fd >= 0) {
            rc = stream_connect_x(fd, s, display);
            if (rc != 0) {
                svr4_streams_close(fd);
                sys_close(fd);
                fd = rc == -ECONNREFUSED ? -ENXIO : rc;
            } else if (flags & O_NONBLOCK) {
                sys_fcntl(fd, F_SETFL, O_NONBLOCK);
            }
        }
        *result = fd;
        return 1;
    }
    if ((strncmp(path, SVR4_DEV_X_PREFIX, strlen(SVR4_DEV_X_PREFIX)) == 0 &&
         parse_display(path + strlen(SVR4_DEV_X_PREFIX), "R", &display)) ||
        (strncmp(path, SVR4_X_SOCKET, strlen(SVR4_X_SOCKET)) == 0 &&
         parse_display(path + strlen(SVR4_X_SOCKET), "", &display))) {
        fd = stream_create(AF_UNIX, SOCK_STREAM, STREAM_XREQ, flags, &s);
        if (fd >= 0) {
            s->display = display;
        }
        *result = fd;
        return 1;
    }
    return 0;
}

/* ---- ioctl ----------------------------------------------------------- */

/* A timod or sockmod request carried by I_STR: `buf` holds the `len` bytes
 * given and takes the answer, whose length is returned; negative for an
 * errno. */
static int stream_str(int fd, struct stream *s, int32_t cmd, uint8_t *buf,
                      uint32_t len, uint32_t cap) {
    struct stream_msg *m;
    int32_t w[8];
    int32_t how;

    switch (cmd) {
    case SVR4_SI_GETUDATA:
        w[0] = STREAM_TIDU;
        w[1] = (int32_t)stream_addr_size(s);
        w[2] = STREAM_ADDR;
        w[3] = s->family == AF_INET && s->type == SOCK_STREAM ? 1 : -2;
        w[4] = stream_servtype(s);
        w[5] = 0;                       /* so_state */
        w[6] = 0;                       /* so_options */
        w[7] = STREAM_TIDU;
        /* Seven words in Release 4.0; later ones give room for an eighth.
         * The caller's buffer is as long as it said and no longer. */
        if (len < 7U * 4U || cap < sizeof(w)) {
            return -EINVAL;
        }
        len = len < sizeof(w) ? 7U * 4U : (uint32_t)sizeof(w);
        memcpy(buf, w, len);
        return (int)len;
    case SVR3_TI_GETINFO:
    case SVR3_TI_BIND:
    case SVR3_TI_UNBIND:
    case SVR3_TI_OPTMGMT:
    case SVR4_TI_GETINFO:
    case SVR4_TI_BIND:
    case SVR4_TI_UNBIND:
    case SVR4_TI_OPTMGMT:
        if (len < 4) {
            return -EINVAL;
        }
        if (cmd == SVR4_TI_GETINFO || cmd == SVR3_TI_GETINFO) {
            reply_info(s, 1);
        } else {
            stream_request(fd, s, buf, len);
        }
        /* The answer comes back in the ioctl, not on the stream. */
        if (s->qlen == 0) {
            return -EPROTO;
        }
        m = &s->q[(s->qhead + s->qlen - 1) % STREAM_MSGS];
        s->qlen--;
        if ((uint32_t)m->len > cap) {
            return -EINVAL;
        }
        memcpy(buf, m->ctl, (size_t)m->len);
        return m->len;
    case SVR4_SI_SHUTDOWN:
        if (len < 4) {
            return -EINVAL;
        }
        how = get32(buf);
        how = sys_shutdown(fd, how);
        return how < 0 ? how : 0;
    case SVR4_SI_SETMYNAME:
    case SVR4_SI_SETPEERNAME:
    case SVR4_TI_SETMYNAME:
    case SVR4_TI_SETPEERNAME:
        return 0;
    default:
        return -EINVAL;
    }
}

static int64_t ioctl_str(int fd, struct stream *s, uint32_t arg) {
    struct svr4_strioctl ioc;
    uint8_t buf[STREAM_CTL];
    uint32_t len;
    int rc;

    if (user_get(arg, &ioc, sizeof(ioc)) != 0) {
        return -EFAULT;
    }
    len = ioc.ic_len > 0 ? (uint32_t)ioc.ic_len : 0;
    if (len > sizeof(buf)) {
        return -EINVAL;
    }
    memset(buf, 0, sizeof(buf));
    if (len && user_get(ioc.ic_dp, buf, len) != 0) {
        return -EFAULT;
    }
    streams_log("I_STR command", fd, (const uint8_t *)&ioc.ic_cmd, 4);
    streams_log("I_STR", fd, buf, len);
    rc = stream_str(fd, s, ioc.ic_cmd, buf, len, sizeof(buf));
    if (rc < 0) {
        return rc;
    }
    streams_log("I_STR answer", fd, buf, (uint32_t)rc);
    /* A provider's refusal is not a failed ioctl: timod returns it as the
     * call's value, the errno above the t_errno. */
    if (rc >= 16 && get32(buf) == SVR4_T_ERROR_ACK) {
        return ((int64_t)(get32(buf + 12) & 0xff) << 8) |
               (get32(buf + 8) & 0xff);
    }
    if (rc && user_put(ioc.ic_dp, buf, (uint32_t)rc) != 0) {
        return -EFAULT;
    }
    ioc.ic_len = rc;
    return user_put(arg, &ioc, sizeof(ioc));
}

/* TI_GETMYNAME and TI_GETPEERNAME: the address into the caller's netbuf. */
static int64_t ioctl_name(int fd, int peer, uint32_t arg) {
    struct svr4_strbuf nb;
    uint8_t addr[STREAM_ADDR];
    socklen_t len = sizeof(addr);
    int rc;

    if (user_get(arg, &nb, sizeof(nb)) != 0) {
        return -EFAULT;
    }
    rc = kern_sockname(fd, peer, addr, &len);
    if (rc != 0) {
        return rc;
    }
    if (len > sizeof(addr)) {
        len = sizeof(addr);
    }
    if (nb.maxlen < 0 || len > (socklen_t)nb.maxlen) {
        len = nb.maxlen < 0 ? 0 : (socklen_t)nb.maxlen;
    }
    if (len && user_put(nb.buf, addr, len) != 0) {
        return -EFAULT;
    }
    nb.len = (int32_t)len;
    return user_put(arg, &nb, sizeof(nb));
}

static int64_t ioctl_fdinsert(struct stream *s, uint32_t arg) {
    struct svr4_strfdinsert ins;
    struct stream *other;

    if (user_get(arg, &ins, sizeof(ins)) != 0) {
        return -EFAULT;
    }
    other = stream_of(ins.fildes);
    if (!other || other->kind != STREAM_SPX) {
        return -EINVAL;
    }
    if (s->kind == STREAM_SPX) {
        /* Two ends made into one pipe.  Which end the server gets, and so
         * which is the client's, is not known until one is sent. */
        s->pair = other->node;
        other->pair = s->node;
        return 0;
    }
    if (s->kind != STREAM_XREQ) {
        return -EINVAL;
    }
    return other->connected ? 0
                            : stream_connect_x(ins.fildes, other, s->display);
}

/*
 * I_SENDFD on the server's request stream: the descriptor sent is the
 * server's end of a pipe made with I_FDINSERT, and the end the client
 * keeps is the one to connect.
 */
static int64_t ioctl_sendfd(struct stream *s, int sent) {
    struct stream *end = stream_of(sent);
    int fd;

    if (s->kind != STREAM_XREQ || !end || end->kind != STREAM_SPX ||
        !end->pair) {
        return -EINVAL;
    }
    for (fd = 0; fd < MAX_FD; fd++) {
        if (fd_node(fd) == end->pair) {
            struct stream *mine = stream_of(fd);

            if (!mine) {
                break;
            }
            return mine->connected ? 0
                                   : stream_connect_x(fd, mine, s->display);
        }
    }
    return -EINVAL;
}

int svr4_streams_ioctl(struct sysv386_frame *f, int64_t *result) {
    int fd = (int)f->a[0];
    uint32_t arg = f->a[2];
    struct stream *s = stream_of(fd);
    char name[SVR4_FMNAMESZ + 1];
    int32_t value = 0;
    uint32_t i;
    int rc;

    if (!s) {
        return 0;
    }
    switch (f->a[1]) {
    case SVR4_I_PUSH:
    case SVR4_I_FIND:
        memset(name, 0, sizeof(name));
        for (i = 0; i < SVR4_FMNAMESZ; i++) {
            if (user_get(arg + i, &name[i], 1) != 0) {
                *result = -EFAULT;
                return 1;
            }
            if (name[i] == '\0') {
                break;
            }
        }
        if (f->a[1] == SVR4_I_FIND) {
            *result = 0;
            for (i = 0; i < s->nmods; i++) {
                if (strcmp(s->mods[i], name) == 0) {
                    *result = 1;
                }
            }
            return 1;
        }
        if (strcmp(name, "sockmod") != 0 && strcmp(name, "timod") != 0 &&
            strcmp(name, "tirdwr") != 0) {
            *result = -EINVAL;
        } else if (s->nmods >= STREAM_MODS) {
            *result = -ENOSR;
        } else {
            memcpy(s->mods[s->nmods++], name, sizeof(name));
            *result = 0;
        }
        return 1;
    case SVR4_I_POP:
        *result = s->nmods ? 0 : -EINVAL;
        if (s->nmods) {
            s->nmods--;
        }
        return 1;
    case SVR4_I_LOOK:
        *result = s->nmods ? user_put(arg, s->mods[s->nmods - 1],
                                      SVR4_FMNAMESZ + 1)
                           : -EINVAL;
        return 1;
    case SVR4_I_STR:
        *result = ioctl_str(fd, s, arg);
        return 1;
    case SVR4_I_FDINSERT:
        *result = ioctl_fdinsert(s, arg);
        return 1;
    case SVR4_I_SENDFD:
        *result = ioctl_sendfd(s, (int)arg);
        return 1;
    case SVR4_I_NREAD:
    case SVR4_FIONREAD:
        if (sysv386_span(arg, sizeof(value)) != 0) {
            *result = -EFAULT;
            return 1;
        }
        if (s->qlen) {
            value = s->q[s->qhead].len;
            rc = user_put(arg, &value, sizeof(value));
        } else {
            /* The socket answers into the caller's own word: its ioctl
             * copies out, and will not to an address in the kernel. */
            rc = kern_ioctl(fd, FIONREAD, (void *)(uintptr_t)arg);
            if (rc == 0) {
                rc = user_get(arg, &value, sizeof(value));
            }
        }
        /* I_NREAD also says how many messages there are. */
        *result = rc != 0 ? rc
                : f->a[1] == SVR4_I_NREAD ? (value > 0 || s->qlen) : 0;
        return 1;
    case SVR4_FIONBIO:
        if (user_get(arg, &value, sizeof(value)) != 0) {
            *result = -EFAULT;
            return 1;
        }
        rc = sys_fcntl(fd, F_GETFL, 0);
        if (rc >= 0) {
            rc = sys_fcntl(fd, F_SETFL, value ? (rc | O_NONBLOCK)
                                              : (rc & ~O_NONBLOCK));
        }
        *result = rc < 0 ? rc : 0;
        return 1;
    case SVR4_TI_GETMYNAME:
    case SVR4_TI_GETPEERNAME:
        *result = ioctl_name(fd, f->a[1] == SVR4_TI_GETPEERNAME, arg);
        return 1;
    case SVR4_I_GRDOPT:
    case SVR4_I_GWROPT:
    case SVR4_I_GETSIG:
    case SVR4_I_GETCLTIME:
        *result = user_put(arg, &value, sizeof(value));
        return 1;
    case SVR4_I_SRDOPT:
    case SVR4_I_SWROPT:
    case SVR4_I_SETSIG:
    case SVR4_I_SETCLTIME:
    case SVR4_I_FLUSH:
        *result = 0;
        return 1;
    case SVR4_I_CANPUT:
        *result = 1;
        return 1;
    default:
        return 0;
    }
}

/* ---- getmsg, putmsg -------------------------------------------------- */

int64_t svr4_sys_putmsg(struct sysv386_frame *f) {
    int fd = (int)f->a[0];
    struct stream *s = stream_of(fd);
    struct svr4_strbuf ctl = { 0, -1, 0 }, data = { 0, -1, 0 };
    uint8_t req[STREAM_CTL];
    int32_t prim;
    ssize_t n;

    if (!s) {
        return fd_node(fd) ? -ENOSTR : -EBADF;
    }
    if (f->a[1] && user_get(f->a[1], &ctl, sizeof(ctl)) != 0) {
        return -EFAULT;
    }
    if (f->a[2] && user_get(f->a[2], &data, sizeof(data)) != 0) {
        return -EFAULT;
    }
    if (ctl.len >= 4) {
        if ((uint32_t)ctl.len > sizeof(req)) {
            return -ERANGE;
        }
        if (user_get(ctl.buf, req, (uint32_t)ctl.len) != 0) {
            return -EFAULT;
        }
        prim = get32(req);
        if (prim != SVR4_T_DATA_REQ && prim != SVR4_T_EXDATA_REQ) {
            return stream_request(fd, s, req, (uint32_t)ctl.len);
        }
    }
    if (data.len <= 0) {
        return 0;
    }
    if (sysv386_span(data.buf, (uint32_t)data.len) != 0) {
        return -EFAULT;
    }
    n = kern_write(fd, (const char *)(uintptr_t)data.buf, (size_t)data.len);
    return n < 0 ? n : 0;
}

int64_t svr4_sys_getmsg(struct sysv386_frame *f) {
    int fd = (int)f->a[0];
    struct stream *s = stream_of(fd);
    struct svr4_strbuf ctl = { -1, -1, 0 }, data = { -1, -1, 0 };
    int32_t flags = 0, w;
    int64_t more = 0;
    ssize_t n;

    if (!s) {
        return fd_node(fd) ? -ENOSTR : -EBADF;
    }
    if (f->a[1] && user_get(f->a[1], &ctl, sizeof(ctl)) != 0) {
        return -EFAULT;
    }
    if (f->a[2] && user_get(f->a[2], &data, sizeof(data)) != 0) {
        return -EFAULT;
    }
    if (f->a[3] && user_get(f->a[3], &flags, sizeof(flags)) != 0) {
        return -EFAULT;
    }
    ctl.len = -1;
    data.len = -1;

    if (s->qlen) {
        struct stream_msg *m = &s->q[s->qhead];
        int32_t give = m->len;

        if ((flags & SVR4_RS_HIPRI) && !m->hipri) {
            return -EAGAIN;
        }
        streams_log("getmsg", fd, m->ctl, (uint32_t)m->len);
        if (ctl.maxlen >= 0) {
            if (give > ctl.maxlen) {
                give = ctl.maxlen;
                more = SVR4_MORECTL;
            }
            if (give && user_put(ctl.buf, m->ctl, (uint32_t)give) != 0) {
                return -EFAULT;
            }
            ctl.len = give;
        }
        if (more) {
            memmove(m->ctl, m->ctl + give, (size_t)(m->len - give));
            m->len -= give;
        } else {
            flags = m->hipri ? SVR4_RS_HIPRI : 0;
            s->qhead = (s->qhead + 1) % STREAM_MSGS;
            s->qlen--;
        }
        if (data.maxlen >= 0) {
            data.len = 0;
        }
    } else if (flags & SVR4_RS_HIPRI) {
        return -EAGAIN;
    } else if (data.maxlen > 0) {
        if (sysv386_span(data.buf, (uint32_t)data.maxlen) != 0) {
            return -EFAULT;
        }
        n = kern_read(fd, (char *)(uintptr_t)data.buf, (size_t)data.maxlen);
        if (n < 0) {
            return n;
        }
        flags = 0;
        if (n == 0 && !s->eof && ctl.maxlen >= 4) {
            /* The other end is done sending: an orderly release. */
            s->eof = 1;
            w = SVR4_T_ORDREL_IND;
            if (user_put(ctl.buf, &w, sizeof(w)) != 0) {
                return -EFAULT;
            }
            ctl.len = 4;
        } else {
            data.len = (int32_t)n;
        }
    } else {
        return -EAGAIN;
    }

    if (f->a[1] && user_put(f->a[1], &ctl, sizeof(ctl)) != 0) {
        return -EFAULT;
    }
    if (f->a[2] && user_put(f->a[2], &data, sizeof(data)) != 0) {
        return -EFAULT;
    }
    if (f->a[3] && user_put(f->a[3], &flags, sizeof(flags)) != 0) {
        return -EFAULT;
    }
    return more;
}

/* ---- poll, readv, writev --------------------------------------------- */

/*
 * poll(2).  struct pollfd is substrate's and so are the low event bits;
 * POLLRDNORM asks what POLLIN asks, and Release 4's POLLWRBAND is where
 * substrate has POLLWRNORM.  A stream with an answer waiting is readable
 * whatever its socket says.
 */
int64_t svr4_sys_poll(struct sysv386_frame *f) {
    uint32_t nfds = f->a[1], i;
    int timeout = (int)f->a[2];
    struct pollfd *fds;
    uint16_t *asked;
    size_t size = (size_t)nfds * sizeof(*fds);
    size_t asked_size = (size_t)nfds * sizeof(*asked);
    int waiting = 0, rc;

    if (nfds > STREAM_POLL_MAX) {
        return -EINVAL;
    }
    if (nfds == 0) {
        return kern_poll(NULL, 0, timeout);
    }
    fds = kmalloc(size);
    asked = kmalloc(asked_size);
    if (!fds || !asked) {
        rc = -ENOMEM;
        goto out;
    }
    if (user_get(f->a[0], fds, (uint32_t)size) != 0) {
        rc = -EFAULT;
        goto out;
    }
    for (i = 0; i < nfds; i++) {
        struct stream *s = fds[i].fd >= 0 ? stream_of(fds[i].fd) : NULL;
        uint16_t ev = (uint16_t)fds[i].events;

        asked[i] = ev;
        fds[i].events = (short)(ev & (POLLIN | POLLPRI | POLLOUT));
        if (ev & (SVR4_POLLRDNORM | SVR4_POLLRDBAND)) {
            fds[i].events |= POLLIN;
        }
        if (ev & SVR4_POLLWRBAND) {
            fds[i].events |= POLLOUT;
        }
        if (s && s->qlen && (fds[i].events & POLLIN)) {
            waiting++;
        }
    }
    rc = kern_poll(fds, nfds, waiting ? 0 : timeout);
    if (rc < 0) {
        goto out;
    }
    rc = 0;
    for (i = 0; i < nfds; i++) {
        struct stream *s = fds[i].fd >= 0 ? stream_of(fds[i].fd) : NULL;
        uint16_t got = (uint16_t)fds[i].revents;
        uint16_t out = got & (POLLIN | POLLPRI | POLLOUT | POLLERR |
                              POLLHUP | POLLNVAL);

        if (s && s->qlen && (asked[i] & (POLLIN | SVR4_POLLRDNORM))) {
            out |= POLLIN;
        }
        if ((out & POLLIN) && (asked[i] & SVR4_POLLRDNORM)) {
            out |= SVR4_POLLRDNORM;
        }
        if (!(asked[i] & POLLIN)) {
            out &= (uint16_t)~POLLIN;
        }
        if ((got & POLLOUT) && (asked[i] & SVR4_POLLWRBAND)) {
            out |= SVR4_POLLWRBAND;
        }
        if (!(asked[i] & POLLOUT)) {
            out &= (uint16_t)~POLLOUT;
        }
        fds[i].events = (short)asked[i];
        fds[i].revents = (short)out;
        if (out) {
            rc++;
        }
    }
    streams_log("poll", (int)nfds, (const uint8_t *)fds,
                (uint32_t)(size > 32 ? 32 : size));
    if (user_put(f->a[0], fds, (uint32_t)size) != 0) {
        rc = -EFAULT;
    }
out:
    if (fds) {
        kfree(fds, size);
    }
    if (asked) {
        kfree(asked, asked_size);
    }
    return rc;
}

/* readv(2) and writev(2), a vector at a time: what was transferred before
 * a vector that could not be, or that vector's error if none was. */
static int64_t stream_iov(struct sysv386_frame *f, int writing) {
    int fd = (int)f->a[0];
    int32_t count = (int32_t)f->a[2];
    struct svr4_iovec iov[STREAM_IOV_MAX];
    int64_t total = 0;
    ssize_t n;
    int32_t i;

    if (count <= 0 || count > STREAM_IOV_MAX) {
        return -EINVAL;
    }
    if (user_get(f->a[1], iov, (uint32_t)count * sizeof(iov[0])) != 0) {
        return -EFAULT;
    }
    for (i = 0; i < count; i++) {
        if (iov[i].iov_len < 0) {
            return total ? total : -EINVAL;
        }
        if (iov[i].iov_len == 0) {
            continue;
        }
        if (sysv386_span(iov[i].iov_base, (uint32_t)iov[i].iov_len) != 0) {
            return total ? total : -EFAULT;
        }
        n = writing ? kern_write(fd, (const char *)(uintptr_t)iov[i].iov_base,
                                 (size_t)iov[i].iov_len)
                    : kern_read(fd, (char *)(uintptr_t)iov[i].iov_base,
                                (size_t)iov[i].iov_len);
        if (n < 0) {
            return total ? total : n;
        }
        total += n;
        if (n < iov[i].iov_len) {
            break;
        }
    }
    return total;
}

int64_t svr4_sys_readv(struct sysv386_frame *f) {
    return stream_iov(f, 0);
}

int64_t svr4_sys_writev(struct sysv386_frame *f) {
    return stream_iov(f, 1);
}
