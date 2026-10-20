/*
 * scmfail — a sendmsg that fails takes its SCM_RIGHTS descriptors with it.
 *
 * AF_UNIX queued the passed descriptors on the receiver before sending the
 * payload, and left them there when the payload could not be sent: they
 * arrived with the next message instead, and until then held their files
 * open.  Sends a pipe's write end with a datagram too big to send, closes
 * our copy, and checks the pipe reads EOF (no reference left behind) and
 * the next message carries no descriptor.
 *
 * Substrate build; runs on the host too.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void on_alarm(int sig)
{
    (void)sig;
}

int main(void)
{
    int sv[2], p[2], fails = 0;
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);

    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) != 0 || pipe(p) != 0) {
        printf("scmfail: setup failed (errno %d): FAIL\n", errno);
        return 2;
    }

    size_t big = 4 << 20;
    char *payload = calloc(1, big);
    struct iovec iov = { payload, big };
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr m;
    memset(&m, 0, sizeof(m));
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    m.msg_control = cbuf;
    m.msg_controllen = sizeof(cbuf);
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &p[1], sizeof(int));

    ssize_t r = sendmsg(sv[0], &m, 0);
    printf("%s a %zu-byte datagram is refused (%zd, errno %d)\n",
           r < 0 ? "ok   " : "FAIL ", big, r, errno);
    fails += r >= 0;

    close(p[1]);
    char ch;
    alarm(2);
    ssize_t n = read(p[0], &ch, 1);
    alarm(0);
    printf("%s the pipe reads EOF once our write end is closed (%zd)\n",
           n == 0 ? "ok   " : "FAIL ", n);
    fails += n != 0;

    send(sv[0], "hi", 2, 0);
    char data[8];
    char rbuf[CMSG_SPACE(sizeof(int) * 4)];
    struct iovec riov = { data, sizeof(data) };
    struct msghdr rm;
    memset(&rm, 0, sizeof(rm));
    rm.msg_iov = &riov;
    rm.msg_iovlen = 1;
    rm.msg_control = rbuf;
    rm.msg_controllen = sizeof(rbuf);
    alarm(2);
    r = recvmsg(sv[1], &rm, 0);
    alarm(0);
    int got_fd = rm.msg_controllen > 0 && CMSG_FIRSTHDR(&rm) &&
                 CMSG_FIRSTHDR(&rm)->cmsg_type == SCM_RIGHTS;
    printf("%s the next message carries no descriptor (%zd bytes)\n",
           r == 2 && !got_fd ? "ok   " : "FAIL ", r);
    fails += !(r == 2 && !got_fd);

    printf("scmfail: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
