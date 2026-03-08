

#include <arch/i386/syscall.h>
#include <arch/i386/idt.h>

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
#include <sys/copy.h>
#include <sys/errno.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/exec.h>


/* Arch-independent syscalls are now in kern/syscall.c */

extern thread_t *current_thread;
extern process_t *current_process;
extern void signal_handle_pending(registers_t *regs);


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

    exec_maybe_unpin_current_thread(1);

    struct personality *p = perso_lookup(current_process->perso_id);
    if (!p) {
        regs->eax = -38; // ENOSYS
        return;
    }
    uint32_t syscall_num = regs->eax;
    uint32_t saved_edx = regs->edx;

    // Track syscall for SA_RESTART support
    if (current_thread) {
        current_thread->in_syscall = 1;
        current_thread->syscall_num = syscall_num;
        current_thread->syscall_orig_eax = regs->eax;
    }

    uint32_t args[8];
    memset(args, 0, sizeof(args));


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
        args[6] = user_stack[7];
        args[7] = user_stack[8];
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
                        /*
                         * Never dereference user pointers directly from trace path.
                         * For write(fd, buf, len), print up to write length bytes.
                         * For generic strings, use bounded copyinstr.
                         */
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
                                len += sprintf(buf + len, "\"%s%s\"", quote, trunc ? "..." : "");
                            } else {
                                len += sprintf(buf + len, "*%08x", (unsigned int)args[i]);
                            }
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
    
    typedef int64_t (*sys_func_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    sys_func_t func = (sys_func_t)location;
    
    // Dispatch
    int64_t ret = func(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
    regs->eax = (uint32_t)(ret & 0xFFFFFFFF);
    /*
     * Linux i386 int 0x80 stubs may rely on EDX being preserved across
     * 32-bit syscalls (for xchg-based EBX save/restore sequences).
     * Clobbering EDX on plain 32-bit returns can corrupt callee-saved EBX
     * in userspace and crash shells after clone/setpgid.
     */
    if (p->id == PERS_LINUX) {
        regs->edx = saved_edx;
    } else {
        regs->edx = (uint32_t)((ret >> 32) & 0xFFFFFFFF);
    }

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

int sys_fork(void) {
    return arch_fork_with_stack(NULL);
}

int sys_vfork(void) {
    /* Current implementation aliases vfork to fork semantics. */
    return arch_fork_with_stack(NULL);
}

extern void isr128(void); 
void syscall_init(void) {
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0x8E);
}
