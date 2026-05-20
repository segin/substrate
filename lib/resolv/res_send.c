/*
 * res_send.c — send a pre-built DNS query and wait for the reply.
 *
 * UDP only.  Iterates through _res.nsaddr_list[0..nscount), each
 * for up to _res.retry attempts.  Uses select() with a per-attempt
 * timeout of _res.retrans seconds.  Returns the response length on
 * success, -1 with h_errno set on failure.
 */

#include <errno.h>
#include <netinet/in.h>
#include <resolv.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>

int res_send(const unsigned char *query, int querylen,
             unsigned char *answer, int anslen) {
    if (!query || !answer || querylen <= 0 || anslen <= 0)
        return -1;
    if ((_res.options & RES_INIT) == 0 && res_init() < 0) {
        h_errno = NETDB_INTERNAL;
        return -1;
    }

    /* The query's transaction ID is in the first two bytes; reject
     * replies that don't match. */
    uint16_t want_id = ((uint16_t)query[0] << 8) | query[1];

    int last_err = NETDB_INTERNAL;
    for (int ns = 0; ns < _res.nscount; ns++) {
        struct sockaddr_in *server = &_res.nsaddr_list[ns];
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) { last_err = TRY_AGAIN; continue; }

        int sent_ok = 0;
        for (int attempt = 0; attempt < _res.retry; attempt++) {
            ssize_t s = sendto(fd, query, (size_t)querylen, 0,
                               (struct sockaddr *)server, sizeof(*server));
            if (s != querylen) { last_err = TRY_AGAIN; continue; }
            sent_ok = 1;

            fd_set rfds;
            FD_ZERO(&rfds); FD_SET(fd, &rfds);
            struct timeval tv;
            tv.tv_sec = _res.retrans;
            tv.tv_usec = 0;
            int rv = select(fd + 1, &rfds, NULL, NULL, &tv);
            if (rv <= 0) { last_err = TRY_AGAIN; continue; }

            for (;;) {
                struct sockaddr_in from;
                socklen_t fromlen = sizeof(from);
                ssize_t r = recvfrom(fd, answer, (size_t)anslen, 0,
                                     (struct sockaddr *)&from, &fromlen);
                if (r < (ssize_t)NS_HFIXEDSZ) { last_err = TRY_AGAIN; break; }
                /* Match transaction ID. */
                uint16_t got_id = ((uint16_t)answer[0] << 8) | answer[1];
                if (got_id != want_id) continue;     /* late reply; ignore */
                close(fd);
                return (int)r;
            }
        }
        (void)sent_ok;
        close(fd);
    }

    h_errno = last_err;
    return -1;
}
