/*
 * torture_x11_shape.c — minimal AF_UNIX client-server pair that
 * mimics the X protocol startup shape (server listens at a path,
 * client connects, exchanges a setup request and reply, then
 * trades a few protocol messages).
 *
 * Written after the substrate kernel's input_poll / input_read /
 * kern_poll fixes appeared to also somehow affect the X server's
 * connection handling — xtrace gets "X IO ERROR: display closed"
 * the moment it calls XOpenDisplay, while the X server hangs.
 *
 * The torture suite uses two forked processes against a fixed AF_UNIX
 * path; the parent acts as the X-server side and the child as the
 * xtrace-style client.  No actual X protocol is sent — we just want
 * to validate accept / select / read / write under the new io_file
 * plumbing.  If THIS hangs, the io_file plumbing has a bug.  If it
 * passes, the X server's bug is independent of my recent changes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <time.h>

#define SOCK_PATH "/tmp/torture_x11_shape.sock"
#define SETUP_REQ_LEN  64    /* X Connection Setup is ~12 bytes + auth; */
#define SETUP_REPL_LEN 256   /* server reply ~8 bytes + server info */

static int run_server(int listen_fd, int *errp) {
    /* X server's pattern: poll() to wait for clients OR input events,
     * accept the client, write setup reply, then enter the per-client
     * read loop using more poll()s.  (Switched from select to poll
     * to sidestep the substrate cross-toolchain's __restrict
     * qualifier mismatch on select's timeval arg.) */
    for (int round = 0; round < 4; round++) {
        struct pollfd pfd = { .fd = listen_fd, .events = POLLIN };
        int r = poll(&pfd, 1, 5000);   /* 5s timeout */
        if (r <= 0) {
            *errp = errno;
            fprintf(stderr,
                "server: poll returned %d errno=%d (%s)\n",
                r, *errp, strerror(*errp));
            return 1;
        }
        int cfd = accept(listen_fd, NULL, NULL);
        if (cfd < 0) {
            *errp = errno;
            fprintf(stderr, "server: accept failed: %s\n",
                    strerror(*errp));
            return 2;
        }

        /* Mimic the X11 connection-setup dance: read 12 byte header,
         * write a fixed-size reply. */
        unsigned char req[SETUP_REQ_LEN];
        ssize_t n = read(cfd, req, sizeof(req));
        if (n < 0) {
            *errp = errno;
            fprintf(stderr,
                "server: setup-read failed: %s\n", strerror(*errp));
            close(cfd);
            return 3;
        }
        unsigned char reply[SETUP_REPL_LEN];
        memset(reply, 0x42, sizeof(reply));
        reply[0] = 0x01;   /* "Setup successful" status byte */
        if (write(cfd, reply, sizeof(reply)) != (ssize_t)sizeof(reply)) {
            *errp = errno;
            fprintf(stderr,
                "server: setup-write failed: %s\n", strerror(*errp));
            close(cfd);
            return 4;
        }

        /* Hold the client for a moment, then close so the client
         * sees EOF (mimics XCloseDisplay). */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 50 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        close(cfd);
    }
    return 0;
}

static int run_client(int *errp) {
    /* xtrace's pattern: socket() + connect() to server path, write
     * setup request, read reply, expect a connected stream. */
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        *errp = errno;
        fprintf(stderr, "client: socket: %s\n", strerror(*errp));
        return 11;
    }
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCK_PATH, sizeof(sa.sun_path) - 1);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        *errp = errno;
        fprintf(stderr, "client: connect: %s\n", strerror(*errp));
        close(s);
        return 12;
    }

    unsigned char req[SETUP_REQ_LEN];
    memset(req, 0x33, sizeof(req));
    req[0] = 'l';   /* X11 byte-order indicator */
    if (write(s, req, sizeof(req)) != (ssize_t)sizeof(req)) {
        *errp = errno;
        fprintf(stderr, "client: write: %s\n", strerror(*errp));
        close(s);
        return 13;
    }

    /* Read all of the reply — this is where xtrace fails with IO
     * ERROR on the real X server.  If reads return 0 prematurely
     * (server closed half-handshake), Xlib raises IO ERROR. */
    size_t got = 0;
    unsigned char reply[SETUP_REPL_LEN];
    while (got < sizeof(reply)) {
        ssize_t n = read(s, reply + got, sizeof(reply) - got);
        if (n < 0) {
            *errp = errno;
            fprintf(stderr,
                "client: read after %zu bytes: %s\n",
                got, strerror(*errp));
            close(s);
            return 14;
        }
        if (n == 0) {
            *errp = 0;
            fprintf(stderr,
                "client: premature EOF after %zu / %zu bytes — "
                "this is the X IO ERROR shape\n",
                got, sizeof(reply));
            close(s);
            return 15;
        }
        got += (size_t)n;
    }
    if (reply[0] != 0x01) {
        *errp = 0;
        fprintf(stderr,
            "client: reply status byte = 0x%02x, expected 0x01\n",
            reply[0]);
        close(s);
        return 16;
    }
    close(s);
    return 0;
}

int main(void) {
    printf("torture_x11_shape: starting\n");

    /* Tear down any stale socket file. */
    unlink(SOCK_PATH);

    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ls < 0) {
        fprintf(stderr, "socket: %s\n", strerror(errno));
        return 1;
    }
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, SOCK_PATH, sizeof(sa.sun_path) - 1);
    if (bind(ls, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "bind: %s\n", strerror(errno));
        return 1;
    }
    if (listen(ls, 8) < 0) {
        fprintf(stderr, "listen: %s\n", strerror(errno));
        return 1;
    }

    /* Fork: parent = server, child = client. */
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        /* Child: client.  Slight delay to let server settle. */
        close(ls);
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
        for (int i = 0; i < 4; i++) {
            nanosleep(&ts, NULL);
            int e = 0;
            int rc = run_client(&e);
            if (rc != 0) {
                fprintf(stderr,
                    "client iter %d: failed rc=%d errno=%d\n",
                    i, rc, e);
                _exit(rc);
            }
            fprintf(stderr, "client iter %d: OK\n", i);
        }
        _exit(0);
    }

    /* Parent: server. */
    int err = 0;
    int srv_rc = run_server(ls, &err);
    close(ls);
    unlink(SOCK_PATH);

    int st = 0;
    waitpid(pid, &st, 0);
    if (srv_rc != 0) {
        fprintf(stderr, "server failed rc=%d errno=%d\n", srv_rc, err);
        return srv_rc;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        fprintf(stderr, "client failed status=0x%x\n", st);
        return 5;
    }
    printf("torture_x11_shape: PASS\n");
    return 0;
}
