/*
 * compat32.c - convert pointer-carrying structures at the user boundary
 *
 * The process's layout of each structure is in <sys/compat32.h>; the
 * kernel's is its native one.  These copy a structure in or out and
 * convert field by field: user pointers are zero-extended on the way in
 * and narrowed on the way out (they are user addresses, below
 * USER32_VA_END, so nothing is lost).  On the i386 kernel the two layouts
 * coincide and each helper is a copy.
 */
#include <errno.h>
#include <string.h>

#include <sys/compat32.h>
#include <sys/copy.h>

int sigaction_copyin(const void *uaddr, struct sigaction *k) {
    struct sigaction32 u;

    if (copyin(uaddr, &u, sizeof(u)) != 0)
        return EFAULT;
    memset(k, 0, sizeof(*k));
    k->sa_handler = (sig_t)UPTR32(u.sa_handler);
    k->sa_mask = u.sa_mask;
    k->sa_flags = u.sa_flags;
    return 0;
}

int sigaction_copyout(const struct sigaction *k, void *uaddr) {
    struct sigaction32 u;

    memset(&u, 0, sizeof(u));
    u.sa_handler = (uptr32_t)(uintptr_t)k->sa_handler;
    u.sa_mask = k->sa_mask;
    u.sa_flags = k->sa_flags;
    return copyout(&u, uaddr, sizeof(u));
}

void stack_to32(const stack_t *k, stack32_t *u) {
    memset(u, 0, sizeof(*u));
    u->ss_sp = (uptr32_t)(uintptr_t)k->ss_sp;
    u->ss_flags = k->ss_flags;
    u->ss_size = (uint32_t)k->ss_size;
}

int stack_copyin(const void *uaddr, stack_t *k) {
    stack32_t u;

    if (copyin(uaddr, &u, sizeof(u)) != 0)
        return EFAULT;
    memset(k, 0, sizeof(*k));
    k->ss_sp = UPTR32(u.ss_sp);
    k->ss_flags = u.ss_flags;
    k->ss_size = u.ss_size;
    return 0;
}

int stack_copyout(const stack_t *k, void *uaddr) {
    stack32_t u;

    stack_to32(k, &u);
    return copyout(&u, uaddr, sizeof(u));
}

int sigevent_copyin(const void *uaddr, struct sigevent *k) {
    struct sigevent32 u;

    if (copyin(uaddr, &u, sizeof(u)) != 0)
        return EFAULT;
    memset(k, 0, sizeof(*k));
    k->sigev_notify = u.sigev_notify;
    k->sigev_signo = u.sigev_signo;
    /* sival_int and sival_ptr share the word; widening the pointer keeps
     * the integer in its low half either way. */
    k->sigev_value.sival_ptr = UPTR32(u.sigev_value.sival_ptr);
    k->sigev_notify_function =
        (void (*)(union sigval))UPTR32(u.sigev_notify_function);
    k->sigev_notify_attributes = UPTR32(u.sigev_notify_attributes);
    return 0;
}

int thr_param_copyin(const void *uaddr, struct thr_param *k) {
    struct thr_param32 u;

    if (copyin(uaddr, &u, sizeof(u)) != 0)
        return EFAULT;
    memset(k, 0, sizeof(*k));
    k->start_func = (void (*)(void *))UPTR32(u.start_func);
    k->arg = UPTR32(u.arg);
    k->stack_base = UPTR32(u.stack_base);
    k->stack_size = u.stack_size;
    k->tls_base = UPTR32(u.tls_base);
    k->tls_size = u.tls_size;
    k->child_tid = UPTR32(u.child_tid);
    k->parent_tid = UPTR32(u.parent_tid);
    k->flags = u.flags;
    return 0;
}

void siginfo_to32(const siginfo_t *k, siginfo32_t *u) {
    memset(u, 0, sizeof(*u));
    u->si_signo = k->si_signo;
    u->si_errno = k->si_errno;
    u->si_code = k->si_code;
    u->si_pid = k->si_pid;
    u->si_uid = k->si_uid;
    u->si_addr = (uptr32_t)(uintptr_t)k->si_addr;
    u->si_status = k->si_status;
    u->si_value.sival_ptr = (uptr32_t)(uintptr_t)k->si_value.sival_ptr;
}

int siginfo_copyout(const siginfo_t *k, void *uaddr) {
    siginfo32_t u;

    siginfo_to32(k, &u);
    return copyout(&u, uaddr, sizeof(u));
}

int msghdr_copyin(const void *uaddr, struct msghdr *k) {
    struct msghdr32 u;

    if (copyin(uaddr, &u, sizeof(u)) != 0)
        return EFAULT;
    memset(k, 0, sizeof(*k));
    k->msg_name = UPTR32(u.msg_name);
    k->msg_namelen = u.msg_namelen;
    k->msg_iov = UPTR32(u.msg_iov);
    k->msg_iovlen = u.msg_iovlen;
    k->msg_control = UPTR32(u.msg_control);
    k->msg_controllen = u.msg_controllen;
    k->msg_flags = u.msg_flags;
    return 0;
}

int msghdr_copyout(const struct msghdr *k, void *uaddr) {
    struct msghdr32 u;

    memset(&u, 0, sizeof(u));
    u.msg_name = (uptr32_t)(uintptr_t)k->msg_name;
    u.msg_namelen = k->msg_namelen;
    u.msg_iov = (uptr32_t)(uintptr_t)k->msg_iov;
    u.msg_iovlen = k->msg_iovlen;
    u.msg_control = (uptr32_t)(uintptr_t)k->msg_control;
    u.msg_controllen = k->msg_controllen;
    u.msg_flags = k->msg_flags;
    return copyout(&u, uaddr, sizeof(u));
}

int iovec_copyin(const void *uaddr, struct iovec *k, int count) {
    const struct iovec32 *uiov = uaddr;

    for (int i = 0; i < count; i++) {
        struct iovec32 u;

        if (copyin(&uiov[i], &u, sizeof(u)) != 0)
            return EFAULT;
        k[i].iov_base = UPTR32(u.iov_base);
        k[i].iov_len = u.iov_len;
    }
    return 0;
}
