#ifndef _THR_H
#define _THR_H

#include <stdint.h>
#include <sys/types.h>

/* FreeBSD-style thread parameter block.  Passed to thr_new(). */
struct thr_param {
    void    (*start_func)(void *);
    void    *arg;
    void    *stack_base;
    size_t  stack_size;
    void    *tls_base;
    size_t  tls_size;
    long    *child_tid;     /* kernel writes new TID here */
    long    *parent_tid;
    int     flags;
};

/* Substrate-native thread syscall numbers (chosen to overlap the
 * FreeBSD-personality numbers so libthr-style code sees the same
 * dispatch in either personality). */
#define SYS_THR_EXIT      431
#define SYS_THR_SELF      432
#define SYS_THR_KILL      433
#define SYS_THR_SUSPEND   442
#define SYS_THR_WAKE      443
#define SYS_THR_NEW       455
#define SYS_THR_JOIN      457
#define SYS_THR_SET_NAME  464
#define SYS_THR_KILL2     481

#ifdef __cplusplus
extern "C" {
#endif

/* Kernel returns 0 on success or a negative errno.  libsys's typed
 * wrappers translate to the usual -1/errno convention; raw syscall()
 * callers see the negative value. */
struct timespec;
int  thr_new(struct thr_param *param, int param_size);
void thr_exit(long *state) __attribute__((__noreturn__));
long thr_self(void);
int  thr_kill(long id, int sig);
int  thr_kill2(pid_t pid, long id, int sig);
int  thr_suspend(const struct timespec *timeout);
int  thr_wake(long id);
int  thr_set_name(long id, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _THR_H */
