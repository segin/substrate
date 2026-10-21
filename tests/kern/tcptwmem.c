/*
 * tcptwmem -- closed TCP connections waiting out TIME_WAIT do not pin
 * their receive buffers.
 *
 * A connection the user had closed kept its 32 KiB receive ring until the
 * PCB was finally freed, a minute of TIME_WAIT later, although data for a
 * closed socket is only ever answered with a RST.  A burst of short
 * loopback connections therefore pinned hundreds of megabytes, and
 * torture_tcp_lo ran a 512 MiB machine out of memory.  Opens and closes
 * 1500 connections over 127.0.0.1, the client closing first so each of
 * its ends sits in TIME_WAIT, and checks that MemAvailable fell by less
 * than 16 MiB (1500 rings would be 47 MiB).  Needs /proc/meminfo.
 */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CONNS 1500

static long mem_available_kb(void)
{
    char line[128];
    long kb = -1;
    FILE *f = fopen("/proc/meminfo", "r");

    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "MemAvailable:", 13) == 0) {
            kb = strtol(line + 13, NULL, 10);
            break;
        }
    }
    fclose(f);
    return kb;
}

int main(void)
{
    struct sockaddr_in sa;
    socklen_t sl = sizeof sa;
    int one = 1;

    int l = socket(AF_INET, SOCK_STREAM, 0);
    if (l < 0) {
        printf("tcptwmem: socket failed (errno %d): FAIL\n", errno);
        return 1;
    }
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(l, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(l, 64) != 0 ||
        getsockname(l, (struct sockaddr *)&sa, &sl) != 0) {
        printf("tcptwmem: listen failed (errno %d): FAIL\n", errno);
        return 1;
    }

    long before = mem_available_kb();
    if (before < 0) {
        printf("tcptwmem: no MemAvailable in /proc/meminfo: FAIL\n");
        return 1;
    }
    for (int i = 0; i < CONNS; i++) {
        int c = socket(AF_INET, SOCK_STREAM, 0);
        if (c < 0 || connect(c, (struct sockaddr *)&sa, sizeof sa) != 0) {
            printf("tcptwmem: connect %d failed (errno %d): FAIL\n", i, errno);
            return 1;
        }
        int s = accept(l, NULL, NULL);
        if (s < 0) {
            printf("tcptwmem: accept %d failed (errno %d): FAIL\n", i, errno);
            return 1;
        }
        write(c, "x", 1);
        char b;
        read(s, &b, 1);
        close(c);               /* active close: the client end TIME_WAITs */
        usleep(1000);
        close(s);
    }
    usleep(500000);             /* let the last FINs and the reaper run */
    long after = mem_available_kb();
    long used = before - after;

    printf("tcptwmem: %d closed connections hold %ld KiB\n", CONNS, used);
    if (used >= 16 * 1024) {
        printf("tcptwmem: FAIL\n");
        return 1;
    }
    printf("tcptwmem: PASS\n");
    return 0;
}
