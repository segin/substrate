/*
 * lib/c/src/posix_extra3.c — sockets API surface.
 *
 * Three blocks:
 *
 *   1. arpa/inet.h leftovers — inet_addr / inet_ntoa.  Pure
 *      userspace ASCII conversion on top of the existing
 *      inet_pton / inet_ntop.
 *
 *   2. netdb.h legacy resolver entries — local-file lookups
 *      against /etc/hosts, /etc/services, /etc/protocols,
 *      /etc/networks.  Substrate has no DNS yet, so the
 *      gethostbyname path only returns answers that appear in
 *      /etc/hosts.  The setXent/getXent/endXent iterator API is
 *      backed by per-family FILE* state; thread-unsafe (matches
 *      the spec, which says callers must serialize).
 *
 *   3. (historical) accept4 + sockatmark used to live here; the
 *      whole socket family now sits in src/socket.c as real
 *      wrappers over the kernel AF_UNIX / AF_INET socket syscalls.
 *
 * Memory model for the resolver entries: each get*ent / get*by*
 * call returns a pointer to a STATIC struct + STATIC char buffers
 * — the next call overwrites both.  This matches glibc's
 * non-_r behaviour.  Reentrancy needs the _r variants (not yet
 * implemented).
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

char *getpass(const char *prompt) {
    static char buf[128];
    int tty = open("/dev/tty", O_RDWR);
    int fd_in  = tty < 0 ? 0 : tty;
    int fd_out = tty < 0 ? 2 : tty;

    struct termios saved, t;
    int have_tcio = (tcgetattr(fd_in, &saved) == 0);
    if (have_tcio) {
        t = saved;
        t.c_lflag &= ~(unsigned)(ECHO | ECHOE | ECHOK | ECHONL);
        tcsetattr(fd_in, TCSANOW, &t);
    }

    if (prompt) (void)write(fd_out, prompt, strlen(prompt));

    int n = 0;
    while (n + 1 < (int)sizeof(buf)) {
        char c;
        int r = read(fd_in, &c, 1);
        if (r <= 0) { buf[0] = '\0'; break; }
        if (c == '\n' || c == '\r') break;
        buf[n++] = c;
    }
    buf[n] = '\0';

    if (have_tcio) tcsetattr(fd_in, TCSANOW, &saved);
    (void)write(fd_out, "\n", 1);
    if (tty >= 0) close(tty);
    return buf;
}
