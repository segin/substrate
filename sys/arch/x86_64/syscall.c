#include "syscall.h"
#include "../../drivers/video/vga.h" // Warning: VGA might need porting/mapping for 64-bit high half
#include "../../arch/i386/sched.h" // Reusing task structures

// MSRs
#define MSR_STAR      0xC0000081
#define MSR_LSTAR     0xC0000082
#define MSR_FMASK     0xC0000084
#define MSR_KERNEL_GS_BASE 0xC0000102

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = val & 0xFFFFFFFF;
    uint32_t hi = val >> 32;
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
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
    if (!current_task || !current_task->pers) return;

    struct personality *p = current_task->pers;
    if (syscall_number >= p->syscall_count) return;

    void *func = p->syscall_table[syscall_number];
    if (!func) return;

    // Call it. In 64-bit C ABI, args match registers mostly, but we have 6 args here.
    // We cast to a generic 6-arg function pointer.
    typedef uint64_t (*sys_func_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    sys_func_t f = (sys_func_t)func;
    
    // Return value in RAX (handled by assembly stub)
    uint64_t ret = f(arg1, arg2, arg3, arg4, arg5, arg6);
    
    // We need to pass ret back to assembly. 
    // The assembly stub should push RAX or getting it from RAX after call.
    __asm__ volatile("" : : "a"(ret));
}
