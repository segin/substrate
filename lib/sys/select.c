/*
 * select.c - select() syscall wrapper (simplified implementation using poll)
 *
 * Minimal implementation for basic select() usage with timeouts.
 */
#include <limits.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>

int sys_select(int nfds, fd_set * __restrict readfds, fd_set * __restrict writefds,
           fd_set * __restrict exceptfds, struct timeval * __restrict timeout) {
    /* For now, provide a minimal implementation that handles the common case
     * of reading from a single fd with timeout. This is used by readline.c
     * for escape sequence timeouts. Full select() can be implemented later. */
    
    if (nfds < 0 || nfds > FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }
    
    /* Count how many fds we need to monitor */
    int poll_count = 0;
    struct pollfd pfd_buf[64];  /* Stack-allocated buffer for up to 64 fds */
    
    if (nfds > 64) {
        /* Too many fds for our buffer - fall back to immediate check */
        errno = EINVAL;
        return -1;
    }
    
    /* Build poll fd array from select sets */
    for (int i = 0; i < nfds; i++) {
        short events = 0;
        if (readfds && FD_ISSET(i, readfds)) events |= POLLIN;
        if (writefds && FD_ISSET(i, writefds)) events |= POLLOUT;
        if (exceptfds && FD_ISSET(i, exceptfds)) events |= POLLERR;
        
        if (events) {
            pfd_buf[poll_count].fd = i;
            pfd_buf[poll_count].events = events;
            pfd_buf[poll_count].revents = 0;
            poll_count++;
        }
    }
    
    /* Convert timeout to milliseconds.  tv_sec is 64-bit; computing in int
     * lets a multi-week timeout (tv_sec > ~2.1e6) wrap to a negative value,
     * which poll(2) treats as an infinite wait -> select() blocks forever.
     * Compute in 64-bit and clamp to INT_MAX (poll's max finite timeout). */
    int poll_timeout = -1;
    if (timeout) {
        int64_t ms = (int64_t)timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
        if (ms < 0) ms = 0;
        if (ms > INT_MAX) ms = INT_MAX;
        poll_timeout = (int)ms;
    }
    
    /* Call poll syscall */
    int ret = (int)syscall(SYS_POLL, pfd_buf, poll_count, poll_timeout);
    
    if (ret > 0) {
        /* Clear the output sets */
        fd_set result_read, result_write, result_except;
        if (readfds) FD_ZERO(&result_read);
        if (writefds) FD_ZERO(&result_write);
        if (exceptfds) FD_ZERO(&result_except);
        
        /* Copy poll results back to fd_set format */
        for (int i = 0; i < poll_count; i++) {
            int fd = pfd_buf[i].fd;
            short revents = pfd_buf[i].revents;
            
            if (revents & POLLIN && readfds) FD_SET(fd, &result_read);
            if (revents & POLLOUT && writefds) FD_SET(fd, &result_write);
            if (revents & POLLERR && exceptfds) FD_SET(fd, &result_except);
        }
        
        /* Write results back */
        if (readfds) *readfds = result_read;
        if (writefds) *writefds = result_write;
        if (exceptfds) *exceptfds = result_except;
    } else if (ret == 0) {
        /* Timeout - clear output sets */
        if (readfds) FD_ZERO(readfds);
        if (writefds) FD_ZERO(writefds);
        if (exceptfds) FD_ZERO(exceptfds);
    } else {
        /* Error from poll: the raw syscall returns a negative errno,
         * translate to the POSIX -1 + errno contract. */
        if (ret < 0 && ret >= -4095) {
            errno = -ret;
        }
        return -1;
    }

    return ret;
}
