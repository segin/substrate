#ifndef _SYS_POLL_H
#define _SYS_POLL_H

struct pollfd {
    int   fd;
    short events;
    short revents;
};

#define POLLIN      0x0001
#define POLLPRI     0x0002
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010
#define POLLNVAL    0x0020
#define POLLRDNORM  0x0040
#define POLLRDBAND  0x0080
#define POLLWRNORM  0x0100
#define POLLWRBAND  0x0200
#define POLLMSG     0x0400
#define POLLREMOVE  0x1000
#define POLLRDHUP   0x2000

typedef unsigned int nfds_t;

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif
