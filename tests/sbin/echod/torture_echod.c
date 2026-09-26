/*
 * torture_echod.c — echod(8) must serve many clients at the same time.
 *
 * Starts echod on a loopback port, then:
 *   hold   opens NCONN connections and keeps every one of them open, then
 *          talks on them newest-first.  A daemon that serves one client at
 *          a time never answers any but the first.
 *   churn  closes every other connection, opens NCONN more, and talks on
 *          all survivors and newcomers; threads for the closed clients must
 *          have gone away without disturbing the rest.
 *   bulk   streams BULK_BYTES through each of NBULK connections at once,
 *          interleaved, and checks every echoed byte.
 *
 * Runs as init (echod path in argv[1], default /sbin/echod) and prints a
 * "Result:" line.  Build:
 *   i386-unknown-substrate-gcc -O2 -Wall -o tests/sbin/echod/torture_echod \
 *       tests/sbin/echod/torture_echod.c
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT        7007
#define NCONN       48
#define NBULK       8
#define BULK_BYTES  (64 * 1024)
#define WAIT_MS     5000

static int failures;

static void fail(const char *what, int i) {
    printf("  FAIL %s (conn %d, errno %d)\n", what, i, errno);
    failures++;
}

static int dial(void) {
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    if (connect(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(s);
        return -1;
    }
    return s;
}

/* Read exactly len bytes, giving up after WAIT_MS without progress. */
static int recv_all(int s, char *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        struct pollfd p = { .fd = s, .events = POLLIN };
        if (poll(&p, 1, WAIT_MS) <= 0)
            return -1;
        ssize_t n = recv(s, buf + got, len - got, 0);
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
}

static int send_all(int s, const char *buf, size_t len) {
    while (len) {
        ssize_t n = send(s, buf, len, 0);
        if (n <= 0)
            return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static int ping(int s, int i) {
    char out[32], in[32];
    int len = snprintf(out, sizeof(out), "conn %d\n", i);
    if (send_all(s, out, (size_t)len) < 0 ||
        recv_all(s, in, (size_t)len) < 0)
        return -1;
    return memcmp(out, in, (size_t)len) ? -1 : 0;
}

static unsigned char pat(int conn, uint32_t off) {
    return (unsigned char)((off * 2654435761u) >> 24 ^ (uint32_t)conn * 37u);
}

static void run_bulk(void) {
    int fd[NBULK];
    static char in[4096], out[4096];
    uint32_t sent[NBULK] = { 0 }, rcvd[NBULK] = { 0 };

    for (int i = 0; i < NBULK; i++)
        if ((fd[i] = dial()) < 0)
            fail("bulk connect", i);
    if (failures)
        return;

    /* Round-robin 1 KiB writes and drains so all streams are in flight. */
    int left = NBULK;
    while (left) {
        left = 0;
        for (int i = 0; i < NBULK; i++) {
            if (fd[i] < 0)
                continue;
            if (sent[i] < BULK_BYTES) {
                for (int k = 0; k < 1024; k++)
                    out[k] = (char)pat(i, sent[i] + (uint32_t)k);
                if (send_all(fd[i], out, 1024) < 0) {
                    fail("bulk send", i);
                    close(fd[i]);
                    fd[i] = -1;
                    continue;
                }
                sent[i] += 1024;
            }
            uint32_t want = sent[i] - rcvd[i];
            if (want > sizeof(in))
                want = sizeof(in);
            if (recv_all(fd[i], in, want) < 0) {
                fail("bulk echo", i);
                close(fd[i]);
                fd[i] = -1;
                continue;
            }
            for (uint32_t k = 0; k < want; k++)
                if ((unsigned char)in[k] != pat(i, rcvd[i] + k)) {
                    printf("  FAIL bulk data (conn %d, byte %u)\n", i,
                           (unsigned)(rcvd[i] + k));
                    failures++;
                    break;
                }
            rcvd[i] += want;
            if (rcvd[i] < BULK_BYTES)
                left++;
            else {
                close(fd[i]);
                fd[i] = -1;
            }
        }
    }
    printf("  bulk: %d x %d KiB interleaved\n", NBULK, BULK_BYTES / 1024);
}

int main(int argc, char **argv) {
    const char *echod = argc > 1 ? argv[1] : "/sbin/echod";
    int fd[2 * NCONN];

    printf("torture_echod: %s, %d held connections\n", echod, NCONN);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0) {
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0)
            dup2(nul, 1);
        execl(echod, "echod", "7007", (char *)NULL);
        _exit(127);
    }

    /* Wait for the listener. */
    int s = -1;
    for (int t = 0; t < 50 && s < 0; t++) {
        if ((s = dial()) < 0)
            usleep(100000);
    }
    if (s < 0) {
        fail("echod never listened", 0);
        goto out;
    }
    close(s);

    for (int i = 0; i < NCONN; i++)
        if ((fd[i] = dial()) < 0) {
            fail("hold connect", i);
            goto out;
        }
    for (int i = NCONN - 1; i >= 0; i--)
        if (ping(fd[i], i) < 0)
            fail("hold echo", i);
    printf("  hold: %d concurrent\n", NCONN);
    if (failures)
        goto out;

    for (int i = 0; i < NCONN; i += 2) {
        close(fd[i]);
        fd[i] = -1;
    }
    for (int i = NCONN; i < 2 * NCONN; i++)
        if ((fd[i] = dial()) < 0)
            fail("churn connect", i);
    for (int i = 2 * NCONN - 1; i >= 0; i--)
        if (fd[i] >= 0 && ping(fd[i], i) < 0)
            fail("churn echo", i);
    for (int i = 0; i < 2 * NCONN; i++)
        if (fd[i] >= 0)
            close(fd[i]);
    printf("  churn: %d closed, %d reopened\n", NCONN / 2, NCONN);
    if (failures)
        goto out;

    run_bulk();

out:
    if (pid > 0) {
        int st;
        if (waitpid(pid, &st, WNOHANG) == pid) {
            printf("  FAIL echod exited (status 0x%x)\n", st);
            failures++;
        } else {
            kill(pid, SIGKILL);
            waitpid(pid, &st, 0);
        }
    }
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    fflush(stdout);
    for (;;)
        pause();
}
