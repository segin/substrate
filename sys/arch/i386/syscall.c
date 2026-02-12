

#include "syscall.h"
#include "idt.h"

/* NetBSD-style kernel internal includes */
#include <kern/sched.h>
#include <kern/version.h>
#include <kern/panic.h>
#include <kern/console.h>
#include <exec/perso/personality.h>
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
#include <string.h>
#include <stdbool.h>
#include <stdio.h>


/* Arch-independent syscalls are now in kern/syscall.c */

extern thread_t *current_thread;
extern process_t *current_process;
extern int syscall_trace_enabled;
extern void signal_handle_pending(registers_t *regs);

// Linux struct user_desc for set_thread_area
struct user_desc {
    unsigned int entry_number;
    unsigned int base_addr;
    unsigned int limit;
    unsigned int seg_32bit:1;
    unsigned int contents:2;
    unsigned int read_exec_only:1;
    unsigned int limit_in_pages:1;
    unsigned int seg_not_present:1;
    unsigned int useable:1;
};

// GDT TLS entries (Linux uses entries 6, 7, 8 for TLS)
#define GDT_TLS_ENTRIES 3
#define GDT_TLS_START 6

extern void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

int sys_set_thread_area(struct user_desc *u_info) {
    if (!u_info) return -14; // EFAULT
    
    struct user_desc info;
    memcpy(&info, u_info, sizeof(info));
    
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
    
    return 0;
}

// Global flag from main.c
extern int syscall_trace_enabled;

// Global flag from main.c
extern int syscall_trace_enabled;

void syscall_handler(registers_t *regs) {
    if (!current_process) {
        regs->eax = -38; // ENOSYS
        return;
    }

    // Save regs pointer for special syscalls like fork
    if (current_thread) {
        current_thread->syscall_regs = regs;
    }

    struct personality *p = perso_lookup(current_process->perso_id);
    if (!p) {
        regs->eax = -38; // ENOSYS
        return;
    }
    uint32_t syscall_num = regs->eax;

    // Track syscall for SA_RESTART support
    if (current_thread) {
        current_thread->in_syscall = 1;
        current_thread->syscall_num = syscall_num;
        current_thread->syscall_orig_eax = regs->eax;
    }

    uint32_t args[6];


    // Detect ABI (FreeBSD/Native i386 uses stack passing)
    if (p && p->name && (strcmp(p->name, "FreeBSD") == 0 || strcmp(p->name, "substrate") == 0 || strcmp(p->name, "AT&T UNIX SVR4") == 0)) {
        // FreeBSD/Native args are on stack just above return address (ESP+4)
        uint32_t *user_stack = (uint32_t *)(uintptr_t)regs->useresp;
        args[0] = user_stack[1];
        args[1] = user_stack[2];
        args[2] = user_stack[3];
        args[3] = user_stack[4];
        args[4] = user_stack[5];
        args[5] = user_stack[6];
    } else {
        // Default / Linux ABI (Registers)
        args[0] = regs->ebx;
        args[1] = regs->ecx;
        args[2] = regs->edx;
        args[3] = regs->esi;
        args[4] = regs->edi;
        args[5] = regs->ebp;
    }

    if (syscall_trace_enabled) {
        char buf[512];
        const char *name = (p->syscall_names && syscall_num < p->syscall_count) ? p->syscall_names[syscall_num] : NULL;
        struct syscall_fmt *fmt = (p->syscall_fmts && syscall_num < p->syscall_count) ? &p->syscall_fmts[syscall_num] : NULL;

        // Print Header
        // "SYSCALL: PID=1, Personality=Linux"
        char *pers_name = p->name ? (char*)p->name : "Unknown";
        sprintf(buf, "SYSCALL: PID=%d, Personality=%s\n", current_process->pid, pers_name);
        kprint(buf);

        // Print Call start
        // "sys_write(0, "val", 14)"
        int len = 0;
        if (name) len += sprintf(buf + len, "sys_%s(", name);
        else len += sprintf(buf + len, "sys_%d(", syscall_num);
        
        if (fmt && fmt->nargs > 0) {
            for (int i = 0; i < fmt->nargs; i++) {
                if (i > 0) len += sprintf(buf + len, ", ");
                switch (fmt->arg_types[i]) {
                    case ARG_INT: len += sprintf(buf + len, "%d", (int)args[i]); break;
                    case ARG_HEX: len += sprintf(buf + len, "0x%x", (unsigned int)args[i]); break;
                    case ARG_PTR: len += sprintf(buf + len, "*%08x", (unsigned int)args[i]); break;
                    case ARG_LONG: {
                        // Combine two 32-bit values into one 64-bit (lo, hi order on i386)
                        int64_t val64 = ((int64_t)args[i+1] << 32) | args[i];
                        len += sprintf(buf + len, "%lld", (long long)val64);
                        i++; // Skip next slot since we consumed it
                        break;
                    }
                    case ARG_STR: 
                        // Safe(ish) string print
                        if (args[i] && args[i] > 0x1000) { 
                            char quote[64];
                            char *usr = (char*)args[i];
                            int q = 0;
                            for(; q < 60 && usr[q]; q++) quote[q] = usr[q];
                            quote[q] = 0;
                            len += sprintf(buf + len, "\"%s%s\"", quote, q >= 60 ? "..." : "");
                        } else {
                            len += sprintf(buf + len, "NULL");
                        }
                        break;
                    default: len += sprintf(buf + len, "%x", (unsigned int)args[i]); break;
                }
            }
        } else {
             // Fallback
             len += sprintf(buf + len, "0x%x, 0x%x, 0x%x", args[0], args[1], args[2]);
        }
        len += sprintf(buf + len, ")");
        kprint(buf); // Print the call part (no newline yet)
    }
    
    // Check if syscall number is out of range
    if (regs->eax >= p->syscall_count) {
        if (syscall_trace_enabled) {
            char buf[64];
            sprintf(buf, "SYSCALL: Out of range #%u\n", (unsigned int)regs->eax);
            kprint(buf);
        }
        regs->eax = -38; // ENOSYS
        return;
    }
    
    void *location = p->syscall_table[regs->eax];
    
    if (!location) {
        if (syscall_trace_enabled) kprint("SYSCALL: Not Implemented\n");
        regs->eax = -38; // ENOSYS
        return;
    }
    
    typedef int64_t (*sys_func_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    sys_func_t func = (sys_func_t)location;
    
    // Dispatch
    int64_t ret = func(args[0], args[1], args[2], args[3], args[4], args[5]);
    regs->eax = (uint32_t)(ret & 0xFFFFFFFF);
    regs->edx = (uint32_t)((ret >> 32) & 0xFFFFFFFF);

    if (syscall_trace_enabled) {
        char buf[64];
        sprintf(buf, " ret %d\n", (int)regs->eax);
        kprint(buf);
    }

    signal_handle_pending(regs);
    
    // Clear syscall tracking after signals have been handled
    if (current_thread) {
        current_thread->in_syscall = 0;
    }
    
    if (regs->cs == 0x1B && regs->useresp >= 0xC0000000) {
        kprint("SYSCALL RET: Bad User ESP detected!\n");
        panic("Syscall returning with Kernel ESP in User Frame");
    }
}

/* Arch-specific syscalls that require registers_t */
int sys_fork(void) {
    // Fork needs access to the current syscall's register frame
    if (!current_thread || !current_thread->syscall_regs) return -1;
    return sched_fork_process(current_process, current_thread->syscall_regs);
}

int sys_vfork(void) {
    // vfork: child shares parent's address space, parent blocks until child exec/exit
    if (!current_thread || !current_thread->syscall_regs) return -1;
    return sched_fork_process(current_process, current_thread->syscall_regs);
}

extern void isr128(void); 
void syscall_init(void) {
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0x8E);
}
