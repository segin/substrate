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
 *   wireguest relisten <port> <first> <secs> <second> <action>...
 *                                              (listen(first), sleep secs,
 *                                               listen(second), then accept)
 *   wireguest udp <ip> <port> <action>...     (a connect()ed UDP socket)
 *   wireguest mcast <group> <port> <action>... (UDP bound to *:port, joined
 *                                                to group on INADDR_ANY)
 *   wireguest udpany <port> <action>...        (UDP bound to *:port)
 *
 * Actions, executed in order on the connected socket:
 *   readeof      read until EOF (or error), reporting the byte count
 *   read:N       read up to N bytes once
 *   write:TEXT   write TEXT
 *   writen:N     write N octets of a repeating pattern in one call
 *   close        close the socket
 *   shutwr       shutdown(SHUT_WR)
 *   shutrd       shutdown(SHUT_RD)
 *   utimeout:MS  setsockopt(IPPROTO_TCP, TCP_USER_TIMEOUT, MS), read back
 *   sleep:N      sleep N seconds
 *   soerror      getsockopt(SO_ERROR), reporting the value
 *   pollin       poll() for POLLIN with no timeout, reporting revents
 *   pollout:MS   poll() for POLLOUT for up to MS ms, reporting revents
 *   nbwrite:TEXT set O_NONBLOCK and write TEXT once
 *   readn:C:T    read T bytes in reads of at most C bytes
 *   sendto:IP:PORT:TEXT   send TEXT to IP:PORT
 *   sendn:IP:PORT:N       send an N-octet datagram to IP:PORT
 *   ifaddr0      SIOCSIFADDR eth0 0.0.0.0 -- leave the NIC unconfigured
 *
 * A leading "mtu=N" argument first sets eth0's MTU (SIOCSIFMTU).
 *
 * Every step is logged as "guest: ..." on the console so the host side can
 * synchronise on it, and the run ends with "Result: done".
 *
 * The kernel passes a quoted initarg='...' to init as ONE argument, so a
 * lone argument is split on spaces here.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
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
        } else if (strncmp(a, "writen:", 7) == 0) {
            static char big[65536];
            size_t n = (size_t)atol(a + 7);
            if (n > sizeof big) n = sizeof big;
            for (size_t k = 0; k < n; k++) big[k] = (char)('a' + k % 26);
            ssize_t w = write(fd, big, n);
            say("writen %s n=%ld", w < 0 ? strerror(errno) : "ok", (long)w);
        } else if (strcmp(a, "close") == 0) {
            int r = close(fd);
            say("close %s rc=%ld", r < 0 ? strerror(errno) : "ok", r);
            fd = -1;
        } else if (strncmp(a, "utimeout:", 9) == 0) {
            unsigned int ms = (unsigned int)atol(a + 9), back = 0;
            socklen_t bl = sizeof back;
            int r = setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &ms, sizeof ms);
            if (r == 0) r = getsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &back, &bl);
            say("utimeout %s ms=%ld", r < 0 ? strerror(errno) : "ok", (long)back);
        } else if (strcmp(a, "shutrd") == 0) {
            int r = shutdown(fd, SHUT_RD);
            say("shutrd %s rc=%ld", r < 0 ? strerror(errno) : "ok", r);
        } else if (strcmp(a, "shutwr") == 0) {
            int r = shutdown(fd, SHUT_WR);
            say("shutwr %s rc=%ld", r < 0 ? strerror(errno) : "ok", r);
        } else if (strncmp(a, "sendto:", 7) == 0) {
            char ip[32];
            const char *p = a + 7, *c1 = strchr(p, ':');
            const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
            if (!c1 || !c2 || (size_t)(c1 - p) >= sizeof ip) {
                say("bad sendto %s (%ld)", a, (long)i);
                return 1;
            }
            memcpy(ip, p, (size_t)(c1 - p));
            ip[c1 - p] = 0;
            struct sockaddr_in to;
            memset(&to, 0, sizeof to);
            to.sin_family = AF_INET;
            to.sin_port = htons((unsigned short)atoi(c1 + 1));
            to.sin_addr.s_addr = inet_addr(ip);
            ssize_t n = sendto(fd, c2 + 1, strlen(c2 + 1), 0,
                               (struct sockaddr *)&to, sizeof to);
            say("sendto %s n=%ld", n < 0 ? strerror(errno) : "ok", (long)n);
        } else if (strncmp(a, "sendn:", 6) == 0) {
            static char big[4096];
            char ip[32];
            const char *p = a + 6, *c1 = strchr(p, ':');
            const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
            size_t n = c2 ? (size_t)atoi(c2 + 1) : 0;
            if (!c1 || !c2 || (size_t)(c1 - p) >= sizeof ip || n > sizeof big) {
                say("bad sendn %s (%ld)", a, (long)i);
                return 1;
            }
            memcpy(ip, p, (size_t)(c1 - p));
            ip[c1 - p] = 0;
            memset(big, 'x', n);
            struct sockaddr_in to;
            memset(&to, 0, sizeof to);
            to.sin_family = AF_INET;
            to.sin_port = htons((unsigned short)atoi(c1 + 1));
            to.sin_addr.s_addr = inet_addr(ip);
            ssize_t r = sendto(fd, big, n, 0, (struct sockaddr *)&to, sizeof to);
            say("sendn %s n=%ld", r < 0 ? strerror(errno) : "ok", (long)r);
        } else if (strcmp(a, "ifaddr0") == 0) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof ifr);
            strncpy(ifr.ifr_name, "eth0", sizeof ifr.ifr_name - 1);
            struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
            sin->sin_family = AF_INET;
            sin->sin_addr.s_addr = 0;
            int r = ioctl(fd, SIOCSIFADDR, &ifr);
            say("ifaddr0 %s rc=%ld", r < 0 ? strerror(errno) : "ok", r);
        } else if (strcmp(a, "pollin") == 0) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            int r = poll(&pfd, 1, -1);
            say("pollin %s revents=%#lx", r < 0 ? strerror(errno) : "ok",
                (long)pfd.revents);
        } else if (strncmp(a, "readn:", 6) == 0) {
            char buf[4096];
            size_t chunk = (size_t)atoi(a + 6);
            const char *t = strchr(a + 6, ':');
            long want = t ? atol(t + 1) : 0, total = 0;
            if (chunk > sizeof buf) chunk = sizeof buf;
            while (total < want) {
                size_t c = chunk;
                if ((long)c > want - total) c = (size_t)(want - total);
                ssize_t n = read(fd, buf, c);
                if (n <= 0) break;
                total += n;
            }
            say("readn %s total=%ld", total == want ? "ok" : strerror(errno), total);
        } else if (strncmp(a, "pollout:", 8) == 0) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            int r = poll(&pfd, 1, atoi(a + 8));
            say(r == 0 ? "pollout timeout%s revents=%#lx" : "pollout ok%s revents=%#lx",
                r < 0 ? strerror(errno) : "", (long)pfd.revents);
        } else if (strncmp(a, "nbwrite:", 8) == 0) {
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            ssize_t n = write(fd, a + 8, strlen(a + 8));
            say("nbwrite %s n=%ld", n < 0 ? strerror(errno) : "ok", (long)n);
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
    if (argc >= 2 && strncmp(argv[1], "mtu=", 4) == 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof ifr);
        strncpy(ifr.ifr_name, "eth0", sizeof ifr.ifr_name - 1);
        ifr.ifr_mtu = atoi(argv[1] + 4);
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        int r = ioctl(s, SIOCSIFMTU, &ifr);
        close(s);
        say("mtu %s n=%ld", r < 0 ? strerror(errno) : "ok", (long)ifr.ifr_mtu);
        argv[1] = argv[0];
        argv++;
        argc--;
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
        int one = 1;
        /* Broadcast needs SO_BROADCAST (UDP-API-15), as for any real client. */
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) {
            say("udp connected %s%ld", "", 0);
            rc = do_actions(fd, argc - 4, argv + 4);
        } else {
            say("udp connect failed: %s (%ld)", strerror(errno), errno);
        }
    } else if (argc >= 4 && strcmp(argv[1], "mcast") == 0) {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((unsigned short)atoi(argv[3]));
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        struct ip_mreq mr;
        mr.imr_multiaddr.s_addr = inet_addr(argv[2]);
        mr.imr_interface.s_addr = htonl(INADDR_ANY);
        if (fd >= 0 && bind(fd, (struct sockaddr *)&sa, sizeof sa) == 0 &&
            setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof mr) == 0) {
            say("joined %s%ld", argv[2], 0);
            rc = do_actions(fd, argc - 4, argv + 4);
        } else {
            say("mcast setup failed: %s (%ld)", strerror(errno), errno);
        }
    } else if (argc >= 3 && strcmp(argv[1], "udpany") == 0) {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((unsigned short)atoi(argv[2]));
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
        if (fd >= 0 && bind(fd, (struct sockaddr *)&sa, sizeof sa) == 0) {
            say("udpany bound %s%ld", "", atol(argv[2]));
            rc = do_actions(fd, argc - 3, argv + 3);
        } else {
            say("udpany failed: %s (%ld)", strerror(errno), errno);
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
    } else if (argc >= 6 && strcmp(argv[1], "relisten") == 0) {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((unsigned short)atoi(argv[2]));
        int l = socket(AF_INET, SOCK_STREAM, 0);
        if (l >= 0 && bind(l, (struct sockaddr *)&sa, sizeof sa) == 0 &&
            listen(l, atoi(argv[3])) == 0) {
            say("listening %s%ld", "", atol(argv[2]));
            sleep((unsigned)atoi(argv[4]));
            if (listen(l, atoi(argv[5])) == 0) {
                say("relistened backlog=%s%ld", "", atol(argv[5]));
                int fd = accept(l, NULL, NULL);
                if (fd >= 0) {
                    say("accepted %s%ld", "", 0);
                    rc = do_actions(fd, argc - 6, argv + 6);
                } else {
                    say("accept failed: %s (%ld)", strerror(errno), errno);
                }
            } else {
                say("relisten failed: %s (%ld)", strerror(errno), errno);
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
