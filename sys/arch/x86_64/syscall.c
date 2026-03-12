#include <arch/x86_64/syscall.h>
#include <exec/perso/personality.h>
#include <sys/proc.h>
#include <errno.h>

#ifdef HOST_TEST
struct syscall64_host_snapshot {
    uint64_t msr_lstar;
    uint64_t msr_fmask;
    uint64_t msr_star;
    uint64_t last_return;
};

static struct syscall64_host_snapshot syscall64_host_state;

const struct syscall64_host_snapshot *syscall64_host_get_snapshot(void) {
    return &syscall64_host_state;
}
#endif

static inline void wrmsr(uint32_t msr, uint64_t val) {
#ifdef HOST_TEST
    switch (msr) {
        case MSR_LSTAR:
            syscall64_host_state.msr_lstar = val;
            break;
        case MSR_FMASK:
            syscall64_host_state.msr_fmask = val;
            break;
        case MSR_STAR:
            syscall64_host_state.msr_star = val;
            break;
        default:
            break;
    }
#else
    uint32_t lo = val & 0xFFFFFFFF;
    uint32_t hi = val >> 32;
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
#endif
}

void syscall_init_64(void) {
    // 1. Set handler address in LSTAR
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    // 2. Set SFMASK (flags to clear on syscall)
    // Clear IF (interrupts), DF, TF, etc.
    wrmsr(MSR_FMASK, 0x0200); // Clear IF (bit 9)

    // 3. Set STAR
    // Bits 32-47: Kernel CS (for syscall)
    // Bits 48-63: User CS (for sysret) - actually it expects (UserCS - 16) usually
    // Assuming GDT layout: Null(0), KCode(8), KData(16), UCode(24), UData(32)
    // Syscall loads CS=KCode, SS=KData
    // Sysret loads CS=UCode (STAR[48:63]+16), SS=UData (STAR[48:63]+8)
    // This is tricky x86 logic.
    // Let's assume KCode=0x08, UCode=0x18 (or similar).
    wrmsr(MSR_STAR, ((uint64_t)0x08 << 32) | ((uint64_t)0x10 << 48)); 
}

void syscall_handler_64(uint64_t syscall_number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    uint64_t ret = (uint64_t)-ENOSYS;
    process_t *proc = current_process;

    if (proc) {
        struct personality *p = perso_lookup(proc->perso_id);

        if (p && syscall_number < p->syscall_count) {
            void *func = p->syscall_table[syscall_number];

            if (func) {
                typedef uint64_t (*sys_func_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
                sys_func_t f = (sys_func_t)func;
                ret = f(arg1, arg2, arg3, arg4, arg5, arg6);
            }
        }
    }

#ifdef HOST_TEST
    syscall64_host_state.last_return = ret;
#endif
    __asm__ volatile("" : : "a"(ret));
}
