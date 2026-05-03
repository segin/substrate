#include <sys/types.h>
#include <sys/sysarch.h>
#include <sys/copy.h>
#include <sys/proc.h>
#include <errno.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <arch/i386/syscall.h>
#include <arch/i386/percpu.h>
#include <arch/i386/gdt.h>

extern int vm86_init_bsd(void *args);
extern void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
extern thread_t *current_thread;

/*
 * Set up a GDT data segment with the given base for use as a GS TLS descriptor.
 * Uses GDT_TLS_START slot (one past the standard user segments).
 * The selector is loaded into GS and saved into the thread's trap frame so
 * it survives the iret back to user space.
 */
static int set_gsbase(uint32_t base) {
    /* Ring-3 32-bit data segment: present, DPL=3, writable, 4GB limit */
    gdt_set_gate(GDT_TLS_START, base, 0xFFFFF, 0xF2, 0xC0);

    uint16_t selector = (GDT_TLS_START << 3) | 3;
    __asm__ volatile("mov %0, %%gs" : : "r"(selector));

    /* Persist into the saved trap frame so iret reloads the descriptor. */
    if (current_thread && current_thread->syscall_regs)
        ((registers_t *)current_thread->syscall_regs)->gs = selector;

    return 0;
}

static int get_gsbase(uint32_t *out) {
    gdt_entry_t *e = &THIS_CPU()->gdt[GDT_TLS_START];
    uint32_t base = (uint32_t)e->base_low
                  | ((uint32_t)e->base_middle << 16)
                  | ((uint32_t)e->base_high   << 24);
    return copyout(&base, out, sizeof(base));
}

int sys_sysarch(int op, void *parms) {
    switch (op) {
        case I386_VM86:
            return vm86_init_bsd(parms);

        case I386_SET_GSBASE: {
            uint32_t base;
            if (copyin(parms, &base, sizeof(base)) != 0)
                return -EFAULT;
            return set_gsbase(base);
        }

        case I386_GET_GSBASE:
            return get_gsbase(parms);

        case I386_SET_FSBASE:
            /* FS TLS not needed for FreeBSD i386 nologin; ignore silently */
            return 0;

        case I386_GET_FSBASE:
            return -EINVAL;

        default:
            return -EINVAL;
    }
}
