/*
 * sys/umtx.h — kernel interface to the FreeBSD _umtx_op(2) backing.
 *
 * Implemented in sys/kern/umtx.c.  The FreeBSD personality's _umtx_op
 * wrapper marshals the userland call into kern_umtx_op(); the FreeBSD
 * thr_exit path uses kern_umtx_wake() to release a joiner parked on the
 * exiting thread's tid word.
 */
#ifndef _SYS_UMTX_H
#define _SYS_UMTX_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dispatch one _umtx_op() request.  Returns 0 on success or a negative
 * errno (the BSD personality translates that to CF=1 + positive errno).
 */
int kern_umtx_op(void *obj, int op, unsigned long val, void *uaddr, void *uaddr2);

/*
 * Wake up to `n` threads parked on the umtx word at `uaddr` (virtual user
 * address).  Used both by kern_umtx_op (UMTX_OP_WAKE*) and by thr_exit to
 * release a pthread_join() waiter.
 */
int kern_umtx_wake(void *uaddr, int n);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_UMTX_H */
