/*
 * compat32.c - convert pointer-carrying structures at the user boundary
 *
 * The process's layout of each structure is in <sys/compat32.h>; the
 * kernel's is its native one.  These copy a structure in or out and
 * convert field by field: user pointers are zero-extended on the way in
 * and narrowed on the way out (they are user addresses, below
 * USER32_VA_END, so nothing is lost).  On the i386 kernel the two layouts
 * coincide and each helper is a copy.
 *
 * A native 64-bit process has a third layout, the LP64 one of
 * <sys/amd64_abi.h>, and each helper picks it by the calling process.  The
 * helpers further down do the same for scalars and small records without
 * pointers -- a size_t, a struct timespec, a struct sched_param -- that a
 * handler reads or writes in the middle of its work, where a wrapper around
 * the whole call (exec/perso/perso_native64.c) has nothing to convert.
 */
#include <errno.h>
#include <string.h>

#include <sys/amd64_abi.h>
#include <sys/compat32.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/copy.h>

int sigaction_copyin(const void *uaddr, struct sigaction *k) {
    struct sigaction32 u;

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_sigaction u64;

        if (copyin(uaddr, &u64, sizeof(u64)) != 0)
            return EFAULT;
        memset(k, 0, sizeof(*k));
        k->sa_handler = (sig_t)(uintptr_t)u64.sa_handler;
        k->sa_mask = u64.sa_mask.bits[0];
        k->sa_flags = u64.sa_flags;
        return 0;
    }
#endif

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

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_sigaction u64;

        memset(&u64, 0, sizeof(u64));
        u64.sa_handler = (uint64_t)(uintptr_t)k->sa_handler;
        u64.sa_mask.bits[0] = k->sa_mask;
        u64.sa_flags = k->sa_flags;
        return copyout(&u64, uaddr, sizeof(u64));
    }
#endif

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

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_stack u64;

        if (copyin(uaddr, &u64, sizeof(u64)) != 0)
            return EFAULT;
        memset(k, 0, sizeof(*k));
        k->ss_sp = (void *)(uintptr_t)u64.ss_sp;
        k->ss_flags = u64.ss_flags;
        k->ss_size = (size_t)u64.ss_size;
        return 0;
    }
#endif

    if (copyin(uaddr, &u, sizeof(u)) != 0)
        return EFAULT;
    memset(k, 0, sizeof(*k));
    k->ss_sp = UPTR32(u.ss_sp);
    k->ss_flags = u.ss_flags;
    k->ss_size = u.ss_size;
    return 0;
}

void stack_to_amd64(const stack_t *k, struct amd64_stack *u) {
    memset(u, 0, sizeof(*u));
    u->ss_sp = (uint64_t)(uintptr_t)k->ss_sp;
    u->ss_size = k->ss_size;
    u->ss_flags = k->ss_flags;
}

void siginfo_to_amd64(const siginfo_t *k, struct amd64_siginfo *u) {
    memset(u, 0, sizeof(*u));
    u->si_signo = k->si_signo;
    u->si_errno = k->si_errno;
    u->si_code = k->si_code;
    u->si_pid = k->si_pid;
    u->si_uid = k->si_uid;
    u->si_status = k->si_status;
    u->si_addr = (uint64_t)(uintptr_t)k->si_addr;
    u->si_value = (uint64_t)(uintptr_t)k->si_value.sival_ptr;
}

int stack_copyout(const stack_t *k, void *uaddr) {
    stack32_t u;

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_stack u64;

        stack_to_amd64(k, &u64);
        return copyout(&u64, uaddr, sizeof(u64));
    }
#endif

    stack_to32(k, &u);
    return copyout(&u, uaddr, sizeof(u));
}

int sigevent_copyin(const void *uaddr, struct sigevent *k) {
    struct sigevent32 u;

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_sigevent u64;

        if (copyin(uaddr, &u64, sizeof(u64)) != 0)
            return EFAULT;
        memset(k, 0, sizeof(*k));
        k->sigev_notify = u64.sigev_notify;
        k->sigev_signo = u64.sigev_signo;
        k->sigev_value.sival_ptr = (void *)(uintptr_t)u64.sigev_value;
        k->sigev_notify_function =
            (void (*)(union sigval))(uintptr_t)u64.sigev_notify_function;
        k->sigev_notify_attributes =
            (void *)(uintptr_t)u64.sigev_notify_attributes;
        return 0;
    }
#endif

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

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_thr_param u64;

        if (copyin(uaddr, &u64, sizeof(u64)) != 0)
            return EFAULT;
        memset(k, 0, sizeof(*k));
        k->start_func = (void (*)(void *))(uintptr_t)u64.start_func;
        k->arg = (void *)(uintptr_t)u64.arg;
        k->stack_base = (void *)(uintptr_t)u64.stack_base;
        k->stack_size = (size_t)u64.stack_size;
        k->tls_base = (void *)(uintptr_t)u64.tls_base;
        k->tls_size = (size_t)u64.tls_size;
        k->child_tid = (long *)(uintptr_t)u64.child_tid;
        k->parent_tid = (long *)(uintptr_t)u64.parent_tid;
        k->flags = u64.flags;
        return 0;
    }
#endif

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

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_siginfo u64;

        siginfo_to_amd64(k, &u64);
        return copyout(&u64, uaddr, sizeof(u64));
    }
#endif

    siginfo_to32(k, &u);
    return copyout(&u, uaddr, sizeof(u));
}

int msghdr_copyin(const void *uaddr, struct msghdr *k) {
    struct msghdr32 u;

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_msghdr u64;

        if (copyin(uaddr, &u64, sizeof(u64)) != 0)
            return EFAULT;
        memset(k, 0, sizeof(*k));
        k->msg_name = (void *)(uintptr_t)u64.msg_name;
        k->msg_namelen = (int)u64.msg_namelen;
        k->msg_iov = (void *)(uintptr_t)u64.msg_iov;
        k->msg_iovlen = u64.msg_iovlen;
        k->msg_control = (void *)(uintptr_t)u64.msg_control;
        k->msg_controllen = (int)u64.msg_controllen;
        k->msg_flags = u64.msg_flags;
        return 0;
    }
#endif

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

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_msghdr u64;

        memset(&u64, 0, sizeof(u64));
        u64.msg_name = (uint64_t)(uintptr_t)k->msg_name;
        u64.msg_namelen = (uint32_t)k->msg_namelen;
        u64.msg_iov = (uint64_t)(uintptr_t)k->msg_iov;
        u64.msg_iovlen = k->msg_iovlen;
        u64.msg_control = (uint64_t)(uintptr_t)k->msg_control;
        u64.msg_controllen = (uint32_t)k->msg_controllen;
        u64.msg_flags = k->msg_flags;
        return copyout(&u64, uaddr, sizeof(u64));
    }
#endif

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

void msghdr_out_fields(void *uaddr, struct msghdr_out *out) {
    struct msghdr32 *u = uaddr;

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_msghdr *u64 = uaddr;

        out->msg_namelen = &u64->msg_namelen;
        out->msg_controllen = &u64->msg_controllen;
        out->msg_flags = &u64->msg_flags;
        return;
    }
#endif

    out->msg_namelen = &u->msg_namelen;
    out->msg_controllen = &u->msg_controllen;
    out->msg_flags = &u->msg_flags;
}

int usize_copyin(const void *uaddr, size_t *k) {
    abi_size_t u;

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        uint64_t u64;

        if (copyin(uaddr, &u64, sizeof(u64)) != 0)
            return EFAULT;
        *k = (size_t)u64;
        return 0;
    }
#endif

    if (copyin(uaddr, &u, sizeof(u)) != 0)
        return EFAULT;
    *k = u;
    return 0;
}

int usize_copyout(size_t v, void *uaddr) {
    abi_size_t u = (abi_size_t)v;

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        uint64_t u64 = v;

        return copyout(&u64, uaddr, sizeof(u64));
    }
#endif

    return copyout(&u, uaddr, sizeof(u));
}

int uptr_copyout(uintptr_t v, void *uaddr) {
    uptr32_t u = (uptr32_t)v;

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        uint64_t u64 = v;

        return copyout(&u64, uaddr, sizeof(u64));
    }
#endif

    return copyout(&u, uaddr, sizeof(u));
}

#ifdef SUBSTRATE_ARCH_X86_64
static void timespec_from_amd64(const struct amd64_timespec *u,
                                struct timespec *k) {
    k->tv_sec = u->tv_sec;
    k->tv_nsec = (abi_long_t)u->tv_nsec;
    /* A nanosecond count that does not survive the narrowing is out of
     * range anyway; keep it out of range for the callee's check. */
    if (u->tv_nsec != (int64_t)k->tv_nsec)
        k->tv_nsec = -1;
}

static void timespec_to_amd64(const struct timespec *k,
                              struct amd64_timespec *u) {
    u->tv_sec = k->tv_sec;
    u->tv_nsec = k->tv_nsec;
}
#endif

int timespec_copyin(const void *uaddr, struct timespec *k) {
#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_timespec u64;

        if (copyin(uaddr, &u64, sizeof(u64)) != 0)
            return EFAULT;
        timespec_from_amd64(&u64, k);
        return 0;
    }
#endif

    return copyin(uaddr, k, sizeof(*k));
}

int timespec_copyout(const struct timespec *k, void *uaddr) {
#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_timespec u64;

        timespec_to_amd64(k, &u64);
        return copyout(&u64, uaddr, sizeof(u64));
    }
#endif

    return copyout(k, uaddr, sizeof(*k));
}

int sched_param_copyin(const void *uaddr, struct sched_param *k) {
#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_sched_param u64;

        if (copyin(uaddr, &u64, sizeof(u64)) != 0)
            return EFAULT;
        memset(k, 0, sizeof(*k));
        k->sched_priority = u64.sched_priority;
        k->sched_ss_low_priority = u64.sched_ss_low_priority;
        timespec_from_amd64(&u64.sched_ss_repl_period,
                            &k->sched_ss_repl_period);
        timespec_from_amd64(&u64.sched_ss_init_budget,
                            &k->sched_ss_init_budget);
        k->sched_ss_max_repl = u64.sched_ss_max_repl;
        return 0;
    }
#endif

    return copyin(uaddr, k, sizeof(*k));
}

int sched_param_copyout(const struct sched_param *k, void *uaddr) {
#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_sched_param u64;

        memset(&u64, 0, sizeof(u64));
        u64.sched_priority = k->sched_priority;
        u64.sched_ss_low_priority = k->sched_ss_low_priority;
        timespec_to_amd64(&k->sched_ss_repl_period,
                          &u64.sched_ss_repl_period);
        timespec_to_amd64(&k->sched_ss_init_budget,
                          &u64.sched_ss_init_budget);
        u64.sched_ss_max_repl = k->sched_ss_max_repl;
        return copyout(&u64, uaddr, sizeof(u64));
    }
#endif

    return copyout(k, uaddr, sizeof(*k));
}

int sys_map_copyout(const struct sys_map *k, void *uarray, size_t index) {
#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_map u64;

        memset(&u64, 0, sizeof(u64));
        u64.start = k->start;
        u64.end = k->end;
        u64.flags = k->flags;
        memcpy(u64.name, k->name, sizeof(u64.name));
        return copyout(&u64, (struct amd64_map *)uarray + index, sizeof(u64));
    }
#endif

    return copyout(k, (struct sys_map *)uarray + index, sizeof(*k));
}

int sys_swapinfo_copyout(const struct sys_swapinfo *k, void *uarray,
                         size_t index) {
#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        struct amd64_swapinfo u64;

        memset(&u64, 0, sizeof(u64));
        memcpy(u64.path, k->path, sizeof(u64.path));
        u64.total = k->total;
        u64.used = k->used;
        u64.priority = k->priority;
        return copyout(&u64, (struct amd64_swapinfo *)uarray + index,
                       sizeof(u64));
    }
#endif

    return copyout(k, (struct sys_swapinfo *)uarray + index, sizeof(*k));
}

int iovec_copyin(const void *uaddr, struct iovec *k, int count) {
    const struct iovec32 *uiov = uaddr;

#ifdef SUBSTRATE_ARCH_X86_64
    if (proc_abi_is_amd64()) {
        const struct amd64_iovec *uiov64 = uaddr;

        for (int i = 0; i < count; i++) {
            struct amd64_iovec u;

            if (copyin(&uiov64[i], &u, sizeof(u)) != 0)
                return EFAULT;
            k[i].iov_base = (void *)(uintptr_t)u.iov_base;
            k[i].iov_len = (size_t)u.iov_len;
        }
        return 0;
    }
#endif

    for (int i = 0; i < count; i++) {
        struct iovec32 u;

        if (copyin(&uiov[i], &u, sizeof(u)) != 0)
            return EFAULT;
        k[i].iov_base = UPTR32(u.iov_base);
        k[i].iov_len = u.iov_len;
    }
    return 0;
}
