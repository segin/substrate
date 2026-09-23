/*
 * wireguest.c — guest half of the wire-level TCP test harness.
 *
 * Runs as init.  Performs a scripted sequence of socket operations whose
 * other end is a hand-driven peer on the host (tests/lib/net/wire/wire.py),
 * which sends exact, possibly deliberately malformed, segments and checks
 * the guest's replies on the wire.
 *
 *   wireguest connect <ip> <port> <action>...
 *   wireguest listen <port> <action>...
 *   wireguest udp <ip> <port> <action>...     (a connect()ed UDP socket)
 *
 * Actions, executed in order on the connected socket:
 *   readeof      read until EOF (or error), reporting the byte count
 *   read:N       read up to N bytes once
 *   write:TEXT   write TEXT
 *   close        close the socket
 *   shutwr       shutdown(SHUT_WR)
 *   sleep:N      sleep N seconds
 *   soerror      getsockopt(SO_ERROR), reporting the value
 *
 * Every step is logged as "guest: ..." on the console so the host side can
 * synchronise on it, and the run ends with "Result: done".
 *
 * The kernel passes a quoted initarg='...' to init as ONE argument, so a
 * lone argument is split on spaces here.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void say(const char *fmt, const char *a, long n) {
    printf("guest: ");
    printf(fmt, a, n);
    printf("\n");
    fflush(stdout);
}

static int do_actions(int fd, int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "readeof") == 0) {
            char buf[512];
            long total = 0;
            ssize_t n;
            while ((n = read(fd, buf, sizeof buf)) > 0) total += n;
            say("readeof %s total=%ld", n == 0 ? "EOF" : strerror(errno), total);
        } else if (strncmp(a, "read:", 5) == 0) {
            char buf[4096];
            size_t want = (size_t)atoi(a + 5);
            if (want > sizeof buf) want = sizeof buf;
            ssize_t n = read(fd, buf, want);
            say("read %s n=%ld", n < 0 ? strerror(errno) : "ok", (long)n);
        } else if (strncmp(a, "write:", 6) == 0) {
            ssize_t n = write(fd, a + 6, strlen(a + 6));
            say("write %s n=%ld", n < 0 ? strerror(errno) : "ok", (long)n);
        } else if (strcmp(a, "close") == 0) {
            int r = close(fd);
            say("close %s rc=%ld", r < 0 ? strerror(errno) : "ok", r);
            fd = -1;
        } else if (strcmp(a, "shutwr") == 0) {
            int r = shutdown(fd, SHUT_WR);
            say("shutwr %s rc=%ld", r < 0 ? strerror(errno) : "ok", r);
        } else if (strcmp(a, "soerror") == 0) {
            int err = -1;
            socklen_t el = sizeof(err);
            int r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
            say("soerror %s value=%ld", r < 0 ? strerror(errno) : "ok", (long)err);
        } else if (strncmp(a, "sleep:", 6) == 0) {
            sleep((unsigned)atoi(a + 6));
            say("slept %s%ld", "", atol(a + 6));
        } else {
            say("unknown action %s (%ld)", a, (long)i);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    int rc = 1;
    static char *split[32];
    if (argc == 2 && strchr(argv[1], ' ')) {
        int n = 0;
        split[n++] = argv[0];
        for (char *t = strtok(argv[1], " "); t && n < 31; t = strtok(NULL, " "))
            split[n++] = t;
        split[n] = NULL;
        argc = n;
        argv = split;
    }
    if (argc >= 4 && strcmp(argv[1], "connect") == 0) {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((unsigned short)atoi(argv[3]));
        sa.sin_addr.s_addr = inet_addr(argv[2]);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) {
            say("connected %s%ld", "", 0);
            rc = do_actions(fd, argc - 4, argv + 4);
        } else {
            say("connect failed: %s (%ld)", strerror(errno), errno);
        }
    } else if (argc >= 4 && strcmp(argv[1], "udp") == 0) {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((unsigned short)atoi(argv[3]));
        sa.sin_addr.s_addr = inet_addr(argv[2]);
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) {
            say("udp connected %s%ld", "", 0);
            rc = do_actions(fd, argc - 4, argv + 4);
        } else {
            say("udp connect failed: %s (%ld)", strerror(errno), errno);
        }
    } else if (argc >= 3 && strcmp(argv[1], "listen") == 0) {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((unsigned short)atoi(argv[2]));
        int l = socket(AF_INET, SOCK_STREAM, 0);
        if (l >= 0 && bind(l, (struct sockaddr *)&sa, sizeof sa) == 0 &&
            listen(l, 4) == 0) {
            say("listening %s%ld", "", atol(argv[2]));
            int fd = accept(l, NULL, NULL);
            if (fd >= 0) {
                say("accepted %s%ld", "", 0);
                rc = do_actions(fd, argc - 3, argv + 3);
            } else {
                say("accept failed: %s (%ld)", strerror(errno), errno);
            }
        } else {
            say("listen failed: %s (%ld)", strerror(errno), errno);
        }
    } else {
        say("usage: wireguest connect|listen ... %s%ld", "", 0);
    }
    printf("Result: done\n");
    fflush(stdout);
    for (;;) sleep(60);        /* init must not exit; the host stops qemu */
    return rc;
}
