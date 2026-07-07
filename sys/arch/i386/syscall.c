

#include <arch/i386/syscall.h>
#include <arch/i386/idt.h>
#include <arch/i386/syscall_abi.h>
#include <arch/i386/intr.h>

/* NetBSD-style kernel internal includes */
#include <kern/sched.h>
#include <kern/version.h>
#include <kern/panic.h>
#include <kern/console.h>
#include <kern/main.h>
#include <exec/perso/personality.h>
#include <pm/pm.h>
#include <include/sys/thr.h>
#include <include/sys/acct.h>
#include <include/sys/file.h>
#include <include/sys/proc.h>
#include <include/sys/signal.h>
#include <include/sys/session.h>
#include <vfs/vfs.h>
#include <drivers/console/uart/uart.h>
#include <include/sys/sysinfo.h>

#include <sys/types.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/exec.h>
#include <arch/i386/percpu.h>


/* Arch-independent syscalls are now in kern/syscall.c */

extern thread_t *current_thread;
extern process_t *current_process;
extern void signal_handle_pending(registers_t *regs);


extern void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

/*
 * Translate a substrate (Linux-numbered) errno to the BSD errno numbering
 * used by the FreeBSD / NetBSD / OpenBSD personalities.  errnos 1..34 are
 * identical, but substrate's curated set then diverges from BSD for a
 * handful of values -- most consequentially ENOSYS, which is 38 here but 78
 * on BSD (where 38 is ENOTSOCK).  Returning the wrong number breaks the
 * ubiquitous "ret < 0 && errno != ENOSYS" feature/capability probe idiom
 * (e.g. capsicum's caph_enter / caph_limit_stdio, used by echo(1) and most
 * of the FreeBSD base utilities).
 */
static uint32_t bsd_errno_xlate(uint32_t e) {
    switch (e) {
    case EAGAIN:          return 35;  /* BSD EAGAIN == EWOULDBLOCK */
    case EDEADLK:         return 11;
    case ENOSYS:          return 78;
    case ENOTEMPTY:       return 66;
    case EOPNOTSUPP:      return 45;  /* BSD EOPNOTSUPP == ENOTSUP */
    case ETIMEDOUT:       return 60;
    case EOWNERDEAD:      return 96;
    case ENOTRECOVERABLE: return 95;
    default:              return e;   /* 1..34, ENAMETOOLONG(63), ELOOP(62) match */
    }
}

int sys_set_thread_area(struct user_desc *u_info) {
    if (!u_info) return -14; // EFAULT
    
    struct user_desc info;
    if (copyin(u_info, &info, sizeof(info)) != 0)
        return -14; // EFAULT
    
    // Reject TLS base addresses in kernel space
    if (info.base_addr >= 0xC0000000)
        return -22; // EINVAL
    
    // If entry_number is -1, allocate a new TLS entry
    if (info.entry_number == (unsigned int)-1) {
        info.entry_number = GDT_TLS_START; // Use first TLS slot
    }
    
    // Validate entry number (TLS entries 6, 7, 8)
    if (info.entry_number < GDT_TLS_START || 
        info.entry_number >= GDT_TLS_START + GDT_TLS_ENTRIES) {
        return -22; // EINVAL
    }
    
    // Set up the GDT entry
    // Access byte: 0xF2 = Present, Ring 3, Data segment, Expand-up, Writable
    uint8_t access = 0xF2;
    // Granularity: 0x40 = 32-bit segment, byte granularity
    uint8_t gran = 0x40;
    
    if (info.limit_in_pages) {
        gran |= 0x80; // Page granularity
    }
    if (!info.seg_32bit) {
        gran &= ~0x40; // 16-bit segment
    }
    if (info.seg_not_present) {
        access &= ~0x80; // Not present
    }
    
    gdt_set_gate(info.entry_number, info.base_addr, info.limit, access, gran);
    
    // Load GS with the new selector (entry_number * 8 | RPL 3)
    uint16_t selector = (info.entry_number << 3) | 3;
    __asm__ volatile("mov %0, %%gs" : : "r"(selector));
    
    // Write back the entry number to user
    u_info->entry_number = info.entry_number;
    
    // CRITICAL: Update the saved regs context so that when syscall returns,
    // the POP GS instruction in isr_exit restores this new selector,
    // causing the CPU to reload the cached descriptor base.
    // Otherwise, it restores the old selector (0x33) with old base (0),
    // and TLS accesses (GS:offset) will fault.
    if (current_thread && current_thread->syscall_regs) {
        ((registers_t *)current_thread->syscall_regs)->gs = selector;
    }

    /*
     * Persist the TLS base so arch_switch_to -> i386_load_gs_for_thread
     * re-installs it into GDT_TLS_START on every context switch.
     * Without this, any other personality (or another Linux process)
     * that sets its own TLS overwrites our slot, and on resume our
     * %gs:offset reads return that other thread's TCB — symptom is
     * a SIGSEGV at `mov %gs:0xc, %eax` in glibc/libc-style TLS loads
     * the moment we get scheduled back in after running an FreeBSD or
     * NetBSD child.
     */
    if (current_thread && info.entry_number == GDT_TLS_START) {
        current_thread->gs_base = info.base_addr;
    }

    return 0;
}



/*
 * Per-syscall, per-personality cycle accounting.  Surfaces the cost
 * distribution of every syscall a process makes, so we can tell at a
 * glance which call (and which personality) is responsible for a
 * realtime deficit (e.g. mpg123 stuttering under the netbsd perso).
 *
 * Indexed [personality_id][syscall_nr], capped at sane bounds.  The
 * counters are updated unconditionally — the cost of two rdtsc's
 * plus four 64-bit adds is negligible compared with the dispatched
 * syscall body itself.  Dumping requires `debug=syscall_stats`.
 */
#define SCSTAT_MAX_PERSO    16
#define SCSTAT_MAX_NR      512
struct syscall_stat {
    uint64_t count;
    uint64_t total_cycles;
    uint64_t min_cycles;
    uint64_t max_cycles;
};
static struct syscall_stat scstat[SCSTAT_MAX_PERSO][SCSTAT_MAX_NR];

static inline uint64_t syscall_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void syscall_stats_dump(int reset) {
    extern int kprintf(const char *, ...);
    extern const char *perso_name(int id);
    for (int p = 0; p < SCSTAT_MAX_PERSO; p++) {
        int any = 0;
        for (int n = 0; n < SCSTAT_MAX_NR; n++) {
            if (scstat[p][n].count) { any = 1; break; }
        }
        if (!any) continue;
        const char *pn = perso_name(p);
        kprintf("syscall_stats[%s]:\n", pn ? pn : "?");
        for (int n = 0; n < SCSTAT_MAX_NR; n++) {
            struct syscall_stat *s = &scstat[p][n];
            if (!s->count) continue;
            kprintf("  #%-3d N=%-6llu total=%llu min=%llu max=%llu avg=%llu\n",
                    n,
                    (unsigned long long)s->count,
                    (unsigned long long)s->total_cycles,
                    (unsigned long long)s->min_cycles,
                    (unsigned long long)s->max_cycles,
                    (unsigned long long)(s->total_cycles / s->count));
            if (reset) {
                s->count = 0; s->total_cycles = 0;
                s->min_cycles = 0; s->max_cycles = 0;
            }
        }
    }
}

/*
 * Per-PID trace gate.  Syscall trace is normally global (very noisy);
 * set `trace_pid=<n>` on the kernel cmdline to restrict the
 * SYSCALL: ... lines to a single process.  0 = trace all (legacy).
 */
int syscall_trace_pid = 0;

/*
 * Serial-only trace gate.  When non-zero, the trace path writes
 * directly to the UART via uart_write() instead of kprint(), which
 * would also stamp the VGA console / any other registered backend.
 * Useful when the trace stream is so dense it shoves all other
 * console output offscreen — keep VGA quiet, capture serial via
 * QEMU's -serial.  Set `syscall_trace_serial` on the kernel cmdline.
 */
int syscall_trace_serial = 0;

extern void uart_write(const char *data, size_t size);
extern size_t strlen(const char *);

static inline void syscall_trace_emit(const char *s) {
    /* Serialize the whole write against other CPUs/threads and IRQs so
     * concurrent tracers don't interleave bytes mid-line (the trace is the
     * only place several threads hammer the UART at once). */
    uint32_t f = intr_disable();
    if (syscall_trace_serial) {
        uart_write(s, strlen(s));
    } else {
        extern void kprint(const char *);
        kprint(s);
    }
    intr_restore(f);
}

/*
 * Signal ENOSYS for an unimplemented / out-of-range / NULL-handler syscall,
 * honouring the calling personality's ABI.  BSD personalities report errors
 * via the carry flag (CF=1) with a positive, BSD-numbered errno in EAX; a
 * bare negative EAX with CF clear reads as a SUCCESSFUL return of -38 and
 * leaves errno stale.  That breaks the ubiquitous `ret < 0 && errno ==
 * ENOSYS` feature/capability probe — most visibly FreeBSD login's
 * auditon(A_GETCOND) probe, which then fatally errx()s "could not determine
 * audit condition" and bounces the user back to the login prompt.
 */
static void syscall_emit_enosys(registers_t *regs, struct personality *p) {
    if (p && (p->id == PERS_FREEBSD || p->id == PERS_NETBSD ||
              p->id == PERS_OPENBSD)) {
        regs->eax = bsd_errno_xlate(38);   /* native ENOSYS -> BSD ENOSYS (78) */
        regs->eflags |= 1;                 /* CF = error */
    } else {
        regs->eax = (uint32_t)-38;         /* Linux/native negative-errno */
    }
}

void syscall_handler(registers_t *regs) {
    __asm__ volatile("sti");
    thread_t *cpu_thread = CURRENT_THREAD();
    current_thread = cpu_thread;
    current_process = cpu_thread ? cpu_thread->proc : NULL;

    if (!current_process) {
        regs->eax = -38; // ENOSYS
        return;
    }

    // Save regs pointer for special syscalls like fork
    if (current_thread) {
        current_thread->syscall_regs = regs;
    }

    exec_maybe_unpin_current_thread(1);

    struct personality *p = perso_lookup(current_process->perso_id);
    if (!p) {
        regs->eax = -38; // ENOSYS
        return;
    }
    uint32_t syscall_num = (p->id == PERS_ELKS) ? (uint16_t)regs->eax : regs->eax;
    uint32_t saved_edx = regs->edx;

    // Track syscall for SA_RESTART support
    if (current_thread) {
        current_thread->in_syscall = 1;
        current_thread->frame_replaced = 0;
        current_thread->syscall_num = syscall_num;
        current_thread->syscall_orig_eax = syscall_num;
    }

    uint32_t args[8];
    i386_extract_syscall_args(p, regs, args);

    /* If `trace_pid=N` was set, only emit trace lines for that PID. */
    int trace_this = syscall_trace_enabled &&
                     (syscall_trace_pid == 0 ||
                      current_process->pid == syscall_trace_pid);

    if (trace_this) {
        char buf[512];
        const char *name = (p->syscall_names && syscall_num < p->syscall_count) ? p->syscall_names[syscall_num] : NULL;
        struct syscall_fmt *fmt = (p->syscall_fmts && syscall_num < p->syscall_count) ? &p->syscall_fmts[syscall_num] : NULL;

        // Print Header
        // "SYSCALL: PID=1, Personality=Linux"
        char *pers_name = p->name ? (char*)p->name : "Unknown";
        snprintf(buf, sizeof(buf), "SYSCALL: PID=%d TID=%d, Personality=%s\n",
                 current_process->pid,
                 current_thread ? current_thread->tid : -1, pers_name);
        syscall_trace_emit(buf);

        // Print Call start
        // "sys_write(0, "val", 14)"
        int len = 0;

        #define TRACE_APPEND(...) do { \
            int _r = sizeof(buf) - len; \
            if (_r > 0) { \
                int _w = snprintf(buf + len, _r, __VA_ARGS__); \
                if (_w > 0) { \
                    len += (_w < _r) ? _w : _r - 1; \
                } \
            } \
        } while (0)

        if (name) TRACE_APPEND("sys_%s(", name);
        else TRACE_APPEND("sys_%d(", syscall_num);
        
        if (fmt && fmt->nargs > 0) {
            for (int i = 0; i < fmt->nargs; i++) {
                if (i > 0) TRACE_APPEND(", ");
                switch (fmt->arg_types[i]) {
                    case ARG_INT: TRACE_APPEND("%d", (int)args[i]); break;
                    case ARG_HEX: TRACE_APPEND("0x%x", (unsigned int)args[i]); break;
                    case ARG_PTR: TRACE_APPEND("*%08x", (unsigned int)args[i]); break;
                    case ARG_LONG: {
                        // Combine two 32-bit values into one 64-bit (lo, hi order on i386)
                        int64_t val64 = ((int64_t)args[i+1] << 32) | args[i];
                        TRACE_APPEND("%lld", (long long)val64);
                        i++; // Skip next slot since we consumed it
                        break;
                    }
                    case ARG_STR: 
                        /*
                         * Never dereference user pointers directly from trace path.
                         * For write(fd, buf, len), print up to write length bytes.
                         * For generic strings, use bounded copyinstr.
                         */
                        if (p->id == PERS_ELKS) {
                            TRACE_APPEND("off:0x%x", (unsigned int)args[i]);
                            break;
                        }
                        if (args[i] && args[i] > 0x1000) {
                            char quote[64];
                            size_t copied = 0, show = 0;
                            int ok = 0;
                            int trunc = 0;
                            memset(quote, 0, sizeof(quote));

                            if (name && strcmp(name, "write") == 0 && i == 1 && fmt->nargs >= 3) {
                                int wlen = (int)args[2];
                                if (wlen < 0) wlen = 0;
                                size_t to_copy = (size_t)wlen;
                                if (to_copy > sizeof(quote) - 1) {
                                    to_copy = sizeof(quote) - 1;
                                    trunc = 1;
                                }
                                if (to_copy > 0 && copyin((const void *)(uintptr_t)args[i], quote, to_copy) == 0) {
                                    copied = to_copy;
                                    show = copied;
                                    ok = 1;
                                } else {
                                    copied = 0;
                                    show = 0;
                                    ok = (to_copy == 0);
                                }
                            } else {
                                int ci = copyinstr((const void *)(uintptr_t)args[i], quote, sizeof(quote), &copied);
                                if (ci == 0) {
                                    show = copied;
                                    if (show > 0 && quote[show - 1] == '\0') {
                                        show--;
                                    }
                                    ok = 1;
                                } else if (ci == ENAMETOOLONG) {
                                    trunc = 1;
                                    show = sizeof(quote) - 1;
                                    quote[sizeof(quote) - 1] = '\0';
                                    if (show > 0 && quote[show - 1] == '\0') {
                                        show--;
                                    }
                                    ok = 1;
                                } else {
                                    copied = 0;
                                    show = 0;
                                    ok = 0;
                                }
                            }

                            if (ok) {
                                for (size_t q = 0; q < show; q++) {
                                    unsigned char c = (unsigned char)quote[q];
                                    if (c < 32 || c > 126) quote[q] = '.';
                                }
                                quote[show] = '\0';
                                TRACE_APPEND("\"%s%s\"", quote, trunc ? "..." : "");
                            } else {
                                TRACE_APPEND("*%08x", (unsigned int)args[i]);
                            }
                        } else {
                            TRACE_APPEND("NULL");
                        }
                        break;
                    default: TRACE_APPEND("%x", (unsigned int)args[i]); break;
                }
            }
        } else {
             // Fallback
             TRACE_APPEND("0x%x, 0x%x, 0x%x", args[0], args[1], args[2]);
        }
        TRACE_APPEND(")");

        #undef TRACE_APPEND

        syscall_trace_emit(buf); // Print the call part (no newline yet)
    }
    
    // Check if syscall number is out of range
    if (syscall_num >= p->syscall_count) {
        if (trace_this) {
            char buf[64];
            snprintf(buf, sizeof(buf), "SYSCALL: Out of range #%u\n", (unsigned int)syscall_num);
            syscall_trace_emit(buf);
        }
        syscall_emit_enosys(regs, p);
        return;
    }
    
    if (!p->syscall_table || (uintptr_t)p->syscall_table < 0xC0000000U) {
        if (trace_this) {
            syscall_trace_emit("SYSCALL: Invalid syscall table\n");
        }
        syscall_emit_enosys(regs, p);
        return;
    }

    void *location = p->syscall_table[syscall_num];

    // Check for special sigreturn handling
    if (syscall_num == 119 && p->sigreturn) {
        int sret = p->sigreturn(regs);
        if (current_thread && current_thread->frame_replaced) {
            current_thread->frame_replaced = 0;   /* frame restored — hands off */
        } else {
            regs->eax = (uint32_t)sret;           /* sigreturn failed: report it */
        }
        goto syscall_done;
    }
    if (syscall_num == 173 && p->rt_sigreturn) {
        int sret = p->rt_sigreturn(regs);
        if (current_thread && current_thread->frame_replaced) {
            current_thread->frame_replaced = 0;
        } else {
            regs->eax = (uint32_t)sret;
        }
        goto syscall_done;
    }
    /* Native sigreturn no longer takes a fast path — sys_sigreturn(void *scp)
     * expects a user-space sigcontext pointer (arg from the trampoline's
     * pushed &sigcontext), but the old fast path passed the kernel-side
     * `regs` pointer instead, which copyin then rejected as EFAULT.  The
     * native syscall table maps SYS_SIGRETURN to sys_sigreturn directly,
     * so the normal func(args[0], ...) dispatch below handles it
     * correctly. */

    if (!location) {
        if (trace_this) syscall_trace_emit("SYSCALL: Not Implemented\n");
        syscall_emit_enosys(regs, p);
        return;
    }

    if ((uintptr_t)location < 0xC0000000U) {
        if (trace_this) {
            char buf[96];
            snprintf(buf, sizeof(buf), "SYSCALL: Invalid handler %p for #%u\n",
                    location, (unsigned int)syscall_num);
            syscall_trace_emit(buf);
        }
        syscall_emit_enosys(regs, p);
        return;
    }
    
    typedef int64_t (*sys_func_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    sys_func_t func = (sys_func_t)location;

    // Dispatch (with TSC accounting for syscall_stats)
    uint64_t t0 = syscall_rdtsc();
    int64_t ret = func(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
    uint64_t dt = syscall_rdtsc() - t0;
    if (p->id < SCSTAT_MAX_PERSO && syscall_num < SCSTAT_MAX_NR) {
        struct syscall_stat *s = &scstat[p->id][syscall_num];
        s->count++;
        s->total_cycles += dt;
        if (s->min_cycles == 0 || dt < s->min_cycles) s->min_cycles = dt;
        if (dt > s->max_cycles) s->max_cycles = dt;
    }
    /*
     * sigreturn/rt_sigreturn replaced the whole trapframe with the restored
     * user context.  Every writeback below would corrupt it: the handlers
     * are typed int64_t here while the sigreturns return int, so ret's high
     * half (EDX) is undefined kernel garbage — the native-branch
     * `regs->edx = ret >> 32` was overwriting the restored user EDX on
     * every signal return (Xfbdev died in libXfont/sscanf under the
     * SmartSchedule SIGALRM); the BSD branch would additionally rewrite
     * EFLAGS.CF and turn a "negative" restored EAX into an errno.
     */
    if (current_thread && current_thread->frame_replaced) {
        current_thread->frame_replaced = 0;
        goto syscall_done;
    }

    if (p->id == PERS_ELKS) {
        regs->eax = (uint16_t)ret;
    } else if (p->id == PERS_FREEBSD || p->id == PERS_NETBSD || p->id == PERS_OPENBSD) {
        /*
         * BSD int $0x80 ABI: CF=1 means error (EAX = positive errno value);
         * CF=0 means success (EAX = return value).  Our internal functions
         * return negative errno on error (Linux convention), so translate.
         * Pointer-returning calls (mmap) return (void*)-1 = 0xFFFFFFFF on
         * failure; that is also negative as int32, so CF gets set and EAX=1.
         * The BSD libc mmap stub then calls cerror and returns MAP_FAILED,
         * which the caller detects via the MAP_FAILED pointer check anyway.
         */
        uint32_t low = (uint32_t)(ret & 0xFFFFFFFF);
        if ((int32_t)low < 0) {
            /* positive errno, mapped from substrate to BSD numbering */
            regs->eax = bsd_errno_xlate((uint32_t)(-(int32_t)low));
            regs->eflags |= 1;  /* set CF */
        } else {
            regs->eax = low;
            regs->eflags &= ~1U; /* clear CF */
        }
    } else {
        regs->eax = (uint32_t)(ret & 0xFFFFFFFF);
    }
    /*
     * Linux i386 int 0x80 stubs may rely on EDX being preserved across
     * 32-bit syscalls (for xchg-based EBX save/restore sequences).
     * Clobbering EDX on plain 32-bit returns can corrupt callee-saved EBX
     * in userspace and crash shells after clone/setpgid.
     */
    if (p->id == PERS_LINUX) {
        regs->edx = saved_edx;
    } else if (p->id == PERS_ELKS) {
        regs->edx = (uint16_t)regs->edx;
    } else {
        /*
         * BSD ABI: edx carries the second return value (retval[1]), which the
         * kernel initialises to 0 — only a handful of syscalls set it (pipe ->
         * fd1, lseek -> the off_t high word).  Most handlers here are declared
         * `int` and called through an int64 pointer, so `ret >> 32` is whatever
         * garbage they happened to leave in edx, not 0.  That matters because
         * libc fork()/vfork() distinguish parent from child by testing edx
         * (parent edx==0 -> return child pid; non-zero -> return 0 as the
         * child): a stray non-zero edx makes the PARENT believe it is the child
         * and run the child code path.  Default edx to 0 and only propagate the
         * high word for the syscalls that genuinely return one.
         */
        int edx_is_retval1 =
            (syscall_num == 42) ||                                  /* pipe (both BSD) */
            (p->id == PERS_NETBSD  && syscall_num == 199) ||        /* NetBSD lseek */
            (p->id == PERS_FREEBSD && syscall_num == 478) ||        /* FreeBSD lseek */
            (p->id == PERS_OPENBSD && syscall_num == 199);          /* OpenBSD lseek */
        regs->edx = edx_is_retval1
            ? (uint32_t)((ret >> 32) & 0xFFFFFFFF)
            : 0;
    }

syscall_done:
    if (trace_this) {
        char buf[64];
        snprintf(buf, sizeof(buf), " ret %d\n", (int)regs->eax);
        syscall_trace_emit(buf);
    }

    signal_handle_pending(regs);
    
    // Clear syscall tracking after signals have been handled
    if (current_thread) {
        current_thread->in_syscall = 0;
        if (current_thread->needs_resched &&
            !(current_thread->flags & THREAD_F_NO_PREEMPT)) {
            current_thread->needs_resched = 0;
            sched_yield();
        }
    }
    
    if (regs->cs == 0x1B && regs->useresp >= 0xC0000000) {
        syscall_trace_emit("SYSCALL RET: Bad User ESP detected!\n");
        panic("Syscall returning with Kernel ESP in User Frame");
    }
}

/* Arch-specific syscalls that require registers_t */
int arch_fork_with_stack(void *child_stack) {
    if (!current_thread || !current_thread->syscall_regs) return -1;

    /*
     * Copy the trap frame before handing it to fork logic.
     * Using the live pointer directly is fragile if deeper call chains or
     * asynchronous paths touch the current kernel stack frame.
     */
    registers_t regs = *(registers_t *)current_thread->syscall_regs;
    if (child_stack) {
        regs.useresp = (uint32_t)(uintptr_t)child_stack;
    }
    return sched_fork_process(current_process, &regs);
}

/*
 * arch_clone_thread - create a new thread in the current process from the
 * live syscall trap frame, resuming on child_stack with the given TLS base
 * and CHILD_CLEARTID pointer.  Backs Linux clone(CLONE_THREAD).  Returns the
 * new TID.
 */
int arch_clone_thread(void *child_stack, uint32_t tls_base, int *clear_child_tid) {
    extern int sched_clone_thread(process_t *proc, void *parent_regs,
                                  uint32_t tls_base, int *clear_child_tid);
    if (!current_thread || !current_thread->syscall_regs) return -1;

    registers_t regs = *(registers_t *)current_thread->syscall_regs;
    if (child_stack) {
        regs.useresp = (uint32_t)(uintptr_t)child_stack;
    }
    return sched_clone_thread(current_process, &regs, tls_base, clear_child_tid);
}

int sys_fork(void) {
    return arch_fork_with_stack(NULL);
}

int sys_vfork(void) {
    int child_pid = proc_vfork(current_process, current_thread ? current_thread->syscall_regs : NULL);
    if (child_pid < 0) {
        return child_pid;
    }

    process_t *child = proc_find(child_pid);
    proc_begin_vfork(child);
    return child_pid;
}

extern void isr128(void); 
void syscall_init(void) {
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, IDT_FLAG_USER_INT_GATE);
}
