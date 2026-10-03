/*
 * sys/compat32.h - user-side layouts of structures that carry pointers
 *
 * A process today is a 32-bit (i386 ABI) one on either kernel
 * (<sys/abi32.h>).  Structures it exchanges with the kernel by value are
 * declared in the i386 layout directly; the ones below cannot be, because
 * the kernel keeps real pointers in them -- a signal handler, a thread's
 * start function, the base of an I/O vector -- which are 64 bits wide on
 * the x86_64 kernel.  So the kernel keeps its native structure and these
 * are the user's view of it, converted where it crosses the boundary by
 * the helpers in kern/compat32.c.
 *
 * Every field is fixed-width and each structure is pinned to its i386 size,
 * so the layout is the same on both kernels and the conversion is exact on
 * the i386 one (where it amounts to a copy).  A user pointer is a uptr32_t:
 * the 32-bit address as the process stores it.
 */
#ifndef _SYS_COMPAT32_H
#define _SYS_COMPAT32_H

#include <stdint.h>
#include <stddef.h>
#include <sys/abi32.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/thr.h>
#include <sys/uio.h>

struct sigaction32 {
    uptr32_t sa_handler;
    uint32_t sa_mask;
    int32_t  sa_flags;
};
ABI32_ASSERT_SIZE(struct sigaction32, 12);

typedef struct {
    uptr32_t ss_sp;
    int32_t  ss_flags;
    uint32_t ss_size;
} stack32_t;
ABI32_ASSERT_SIZE(stack32_t, 12);

union sigval32 {
    int32_t  sival_int;
    uptr32_t sival_ptr;
};

typedef struct {
    int32_t  si_signo;
    int32_t  si_errno;
    int32_t  si_code;
    int32_t  si_pid;
    uint32_t si_uid;
    uptr32_t si_addr;
    int32_t  si_status;
    union sigval32 si_value;
    int32_t  _pad[25];
} siginfo32_t;
ABI32_ASSERT_SIZE(siginfo32_t, 132);

struct sigevent32 {
    int32_t  sigev_notify;
    int32_t  sigev_signo;
    union sigval32 sigev_value;
    uptr32_t sigev_notify_function;
    uptr32_t sigev_notify_attributes;
};
ABI32_ASSERT_SIZE(struct sigevent32, 20);

struct thr_param32 {
    uptr32_t start_func;
    uptr32_t arg;
    uptr32_t stack_base;
    uint32_t stack_size;
    uptr32_t tls_base;
    uint32_t tls_size;
    uptr32_t child_tid;     /* int32_t * in the process */
    uptr32_t parent_tid;
    int32_t  flags;
};
ABI32_ASSERT_SIZE(struct thr_param32, 36);

struct iovec32 {
    uptr32_t iov_base;
    uint32_t iov_len;
};
ABI32_ASSERT_SIZE(struct iovec32, 8);

struct msghdr32 {
    uptr32_t msg_name;
    int32_t  msg_namelen;
    uptr32_t msg_iov;
    int32_t  msg_iovlen;
    uptr32_t msg_control;
    int32_t  msg_controllen;
    int32_t  msg_flags;
};
ABI32_ASSERT_SIZE(struct msghdr32, 28);

/* struct robust_list_head (<sys/futex.h>) as the process lays it out; the
 * list it heads is a chain of 32-bit next pointers. */
struct robust_list_head32 {
    uptr32_t list_next;
    int32_t  futex_offset;
    uptr32_t list_op_pending;
};
ABI32_ASSERT_SIZE(struct robust_list_head32, 12);

/*
 * Copy a structure in from, or out to, the process at `uaddr`, converting
 * between its layout and the kernel's.  Each returns 0 or EFAULT, as
 * copyin()/copyout() do.
 */
int sigaction_copyin(const void *uaddr, struct sigaction *k);
int sigaction_copyout(const struct sigaction *k, void *uaddr);
int stack_copyin(const void *uaddr, stack_t *k);
int stack_copyout(const stack_t *k, void *uaddr);
int sigevent_copyin(const void *uaddr, struct sigevent *k);
int thr_param_copyin(const void *uaddr, struct thr_param *k);
int siginfo_copyout(const siginfo_t *k, void *uaddr);
int msghdr_copyin(const void *uaddr, struct msghdr *k);
int msghdr_copyout(const struct msghdr *k, void *uaddr);

/* `count` user I/O vectors at `uaddr` into k[0..count). */
int iovec_copyin(const void *uaddr, struct iovec *k, int count);

/* The process's layout of a siginfo, for building a signal frame. */
void siginfo_to32(const siginfo_t *k, siginfo32_t *u);
void stack_to32(const stack_t *k, stack32_t *u);

#endif /* _SYS_COMPAT32_H */
