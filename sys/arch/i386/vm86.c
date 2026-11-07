#include <errno.h>
#include <string.h>

#include <arch/i386/idt.h>
#include <arch/i386/pmap.h>
#include <arch/i386/vm86.h>
#include <arch/x86-common/io.h>
#include <kern/console.h>
#include <sys/copy.h>
#include <sys/preempt.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/sysarch.h>
#include <sys/types.h>
#include <sys/vm86.h>


/*
 * VM86 Monitor Structure (TASKS.md L578)
 *
 * The V86 monitor manages the virtual machine state for real-mode emulation.
 * It tracks the virtual CPU state, provides interrupt reflection, and handles
 * I/O port virtualization.
 */
struct vm86_monitor {
    uint32_t vflags;        /* Virtual EFLAGS (includes VIF, VIP) */
    uint32_t pending_int;   /* Pending interrupt number, or -1 if none */
    int in_vm86;            /* Currently in VM86 mode */
    int in_kernel_bios;     /* Kernel-mode BIOS call active */
    int req_exit;           /* Request exit from loop */
    
    /* I/O port virtualization callbacks (optional, for advanced use) */
    void *io_context;       /* Opaque context for I/O handlers */
    
    /* Monitor communication (unhandled instructions signal monitor) */
    int signal_pending;     /* Signal monitor for unhandled opcode */
    uint32_t fault_eip;     /* EIP of faulting instruction */
    uint32_t fault_opcode;  /* Opcode that triggered fault */

    struct vm86_regs *out_regs; /* Output registers for kernel BIOS calls */
};

/* Per-process VM86 monitor (NULL if not using VM86) */
static struct vm86_monitor *current_vm86_monitor;
static uint8_t vm86_port_space[65536];

enum {
    VM86_ENTRY_EFLAGS = 0x20200U,   /* VM | IF */
    /* IOPL and NT are never the guest's to set.  IOPL 3 lets CLI, STI,
     * INT n and port I/O run natively instead of trapping here, so the
     * guest would drive the real hardware's ports. */
    VM86_EFLAGS_PRIV  = 0x7000U
};

#ifdef HOST_TEST
static uint8_t *vm86_test_memory;
static size_t vm86_test_memory_size;

void vm86_host_set_memory(void *base, size_t size) {
    vm86_test_memory = (uint8_t *)base;
    vm86_test_memory_size = size;
}
#endif

static uint8_t *vm86_linear_ptr(uint32_t linear, size_t size) {
#ifdef HOST_TEST
    if (!vm86_test_memory || linear > vm86_test_memory_size ||
        size > vm86_test_memory_size - linear) {
        return NULL;
    }
    return vm86_test_memory + linear;
#else
    (void)size;
    return (uint8_t *)(uintptr_t)linear;
#endif
}

/*
 * The guest's interrupt flag is virtual: kept in VIF, while the real IF
 * stays set whatever the guest does, so a guest's CLI cannot stop the
 * CPU taking interrupts.  CLI, STI, PUSHF, POPF, INT n and IRET trap
 * (IOPL is 0) and go through these: a flags image the guest sees or gives
 * has VIF's value in its IF bit.
 */
#define VM86_EFLAGS_IF  0x00000200U
#define VM86_EFLAGS_VIF 0x00080000U

/* The guest's view of its flags: their low 16 bits, IF its virtual one. */
static uint16_t vm86_flags_image(uint32_t eflags) {
    uint16_t image = (uint16_t)(eflags & ~VM86_EFLAGS_IF);

    return (eflags & VM86_EFLAGS_VIF) ? (uint16_t)(image | VM86_EFLAGS_IF) : image;
}

/*
 * @p eflags with the guest's flags image @p image taken in, but for the
 * bits of @p keep: its IF becomes VIF, and the real IF stays set.
 */
static uint32_t vm86_flags_take(uint32_t eflags, uint16_t image, uint32_t keep) {
    uint32_t out = (eflags & keep) | ((uint32_t)image & ~keep);

    out &= ~VM86_EFLAGS_VIF;
    if (image & VM86_EFLAGS_IF)
        out |= VM86_EFLAGS_VIF;
    return out | VM86_EFLAGS_IF;
}

static void vm86_prepare_entry(struct vm86_struct *info) {
    uint32_t guest = info->regs.eflags;

    /* the guest's IF is its VIF; the real IF is set (VM86_ENTRY_EFLAGS) */
    info->regs.eflags = (guest & ~(VM86_EFLAGS_PRIV | VM86_EFLAGS_VIF)) | VM86_ENTRY_EFLAGS |
                        ((guest & VM86_EFLAGS_IF) ? VM86_EFLAGS_VIF : 0);
}

/* A user VM86 guest, as opposed to the kernel's own BIOS calls. */
static int vm86_guest_is_user(void) {
    return !(current_vm86_monitor && current_vm86_monitor->in_kernel_bios);
}

/*
 * Guest memory is the user's low megabyte: read and written with copyin
 * and copyout, which recover from a fault, so a guest whose CS:IP, SS:SP
 * or IVT points at memory it has not mapped (or may not write) gets a
 * signal instead of a ring-0 fault.  The kernel's BIOS calls run on its
 * own identity map of low memory and use it directly.
 */
static int vm86_read(uint32_t linear, void *buf, size_t size) {
#ifndef HOST_TEST
    if (vm86_guest_is_user())
        return copyin((const void *)(uintptr_t)linear, buf, size) == 0 ? 0 : -1;
#endif
    uint8_t *p = vm86_linear_ptr(linear, size);
    if (!p) return -1;
    memcpy(buf, p, size);
    return 0;
}

/*
 * Port I/O a guest traps on.  A user guest's goes to a port space of its
 * own, never the hardware's; the kernel's own BIOS call drives the real
 * ports, as the BIOS it runs is the machine's (a video BIOS waits on the
 * VGA's status port, which a pretend space would never change).
 */
static uint32_t vm86_port_in(uint16_t port, uint32_t size) {
    uint32_t val = 0;

#ifndef HOST_TEST
    if (!vm86_guest_is_user())
        return size == 1 ? inb(port) : size == 2 ? inw(port) : inl(port);
#endif
    for (uint32_t i = 0; i < size; i++)
        val |= (uint32_t)vm86_port_space[(uint16_t)(port + i)] << (8 * i);
    return val;
}

static void vm86_port_out(uint16_t port, uint32_t size, uint32_t val) {
#ifndef HOST_TEST
    if (!vm86_guest_is_user()) {
        if (size == 1)
            outb(port, (uint8_t)val);
        else if (size == 2)
            outw(port, (uint16_t)val);
        else
            outl(port, val);
        return;
    }
#endif
    for (uint32_t i = 0; i < size; i++)
        vm86_port_space[(uint16_t)(port + i)] = (uint8_t)(val >> (8 * i));
}

static int vm86_write(uint32_t linear, const void *buf, size_t size) {
#ifndef HOST_TEST
    if (vm86_guest_is_user())
        return copyout(buf, (void *)(uintptr_t)linear, size) == 0 ? 0 : -1;
#endif
    uint8_t *p = vm86_linear_ptr(linear, size);
    if (!p) return -1;
    memcpy(p, buf, size);
    return 0;
}

/*
 * vm86_monitor_init - Initialize VM86 monitor for current process
 *
 * Call before entering VM86 mode to set up virtualization state.
 */
void vm86_monitor_init(struct vm86_monitor *mon) {
    if (!mon) return;
    
    mon->vflags = 0x200;    /* IF set by default */
    mon->pending_int = (uint32_t)-1;
    mon->in_vm86 = 0;
    mon->in_kernel_bios = 0;
    mon->req_exit = 0;
    mon->io_context = NULL;
    mon->signal_pending = 0;
    mon->fault_eip = 0;
    mon->fault_opcode = 0;
    mon->out_regs = NULL;
    
    current_vm86_monitor = mon;
}

/*
 * vm86_monitor_get - Get current VM86 monitor
 */
struct vm86_monitor *vm86_monitor_get(void) {
    return current_vm86_monitor;
}

/*
 * vm86_monitor_signal_fault - Signal monitor that we have unhandled opcode
 */
void vm86_monitor_signal_fault(uint32_t eip, uint32_t opcode) {
    if (current_vm86_monitor) {
        current_vm86_monitor->signal_pending = 1;
        current_vm86_monitor->fault_eip = eip;
        current_vm86_monitor->fault_opcode = opcode;
    }
}


// Access to the syscall return registers (mapped in syscall.c)
/*
 * sys_vm86: Enter virtual 8086 mode.
 */
static int kern_vm86(struct vm86_struct *k_info);

int sys_vm86(struct vm86_struct *info) {
    if (!info) return -EFAULT;

    struct vm86_struct k_info;
    if (copyin(info, &k_info, sizeof(k_info)) != 0) {
        return -EFAULT;
    }
    return kern_vm86(&k_info);
}

/*
 * Enter VM86 mode with @p k_info, a kernel copy: sys_vm86()'s, or the
 * kernel's own BIOS call's, which copyin() would refuse (it is no user
 * address).
 */
static int kern_vm86(struct vm86_struct *k_info_in) {
    struct vm86_struct k_info = *k_info_in;

    vm86_prepare_entry(&k_info);

    /* A user guest's memory is the process's low megabyte, below where
     * copyin/copyout normally refuse to go; the monitor reaches it with
     * those helpers, so let them (as for a.out images mapped at 0). */
    if (vm86_guest_is_user() && current_process)
        current_process->low_va_valid = 1;

    /* Call assembly helper to build stack frame and execute IRET */
    vm86_enter(&k_info);
    
    /*
     * We return here only when VM86 mode exits via vm86_bios_ret_point
     * (e.g. on HLT in kernel BIOS call).
     */
    return 0;
}

int vm86_init_bsd(void *args) {
    struct i386_vm86_args k_args;

    if (!args) return -EFAULT;
    if (copyin(args, &k_args, sizeof(k_args)) != 0) return -EFAULT;
    
    if (k_args.sub_op == VM86_INIT) {
        // BSD `init` usually enters the mode or prepares it.
        // If it implies entering, we need the struct.
        // Assuming sub_args points to `struct vm86_struct`.
        if (!k_args.sub_args) return -EINVAL;
        return sys_vm86((struct vm86_struct*)k_args.sub_args);
    }
    return -EINVAL;
}

/*
 * End the kernel's own BIOS call: its registers go to the caller's
 * out_regs, and the trap returns not to the guest but to
 * vm86_bios_ret_point, in kernel mode, which returns from vm86_enter() to
 * vm86_bios_call().  The trap frame on the stack is the VM86 one --
 * GS, FS, DS, ES, SS and ESP above EFLAGS, CS and EIP -- which
 * vm86_bios_ret_point drops after IRET has taken the last three.
 */
static void vm86_kernel_bios_exit(registers_t *regs) {
    current_vm86_monitor->in_vm86 = 0;
    current_vm86_monitor->req_exit = 1;

    if (current_vm86_monitor->out_regs) {
        struct vm86_regs *out = current_vm86_monitor->out_regs;
        uint32_t *ext_stack = (uint32_t *)&regs->ss;

        out->eax = regs->eax;
        out->ebx = regs->ebx;
        out->ecx = regs->ecx;
        out->edx = regs->edx;
        out->esi = regs->esi;
        out->edi = regs->edi;
        out->ebp = regs->ebp;
        out->esp = regs->useresp;
        out->eip = regs->eip;
        out->cs  = (unsigned short)regs->cs;
        out->eflags = regs->eflags;
        out->ss = (unsigned short)regs->ss;
        /* VM86 segments (ES, DS, FS, GS) are pushed by the CPU above SS */
        out->es = (unsigned short)ext_stack[1];
        out->ds = (unsigned short)ext_stack[2];
        out->fs = (unsigned short)ext_stack[3];
        out->gs = (unsigned short)ext_stack[4];
    }

    regs->eflags &= ~0x20000U;      /* VM off: IRET stays in kernel mode */
    regs->eip = (uint32_t)vm86_bios_ret_point;
    regs->cs = 0x08;                /* kernel code */
}

/*
 * The guest did something the monitor cannot carry out -- an instruction
 * it does not emulate, or a reference to memory it cannot reach.  Simply
 * returning re-executes the instruction and traps again, forever.  A user
 * guest has nothing to return to (sys_vm86 does not come back), so it is
 * ended by the signal's default action whatever handler it has: a handler
 * would be entered on the guest's VM86 frame.  The kernel's own BIOS call
 * ends, its caller seeing the registers as they were -- a failed call.
 * Returns 1 if a signal was posted, for the trap path to act on.
 */
static int vm86_fail(registers_t *regs, uint32_t opcode, int sig, int code) {
    vm86_monitor_signal_fault(regs->eip, opcode);
    if (!vm86_guest_is_user()) {
        vm86_kernel_bios_exit(regs);
        return 0;
    }
    if (!current_process) return 0;
    current_process->sig_actions[sig - 1].sa_handler = SIG_DFL;
    current_process->sig_actions[sig - 1].sa_flags = 0;
    if (current_thread)
        current_thread->trap_addr = ((regs->cs & 0xFFFFU) << 4) + (regs->eip & 0xFFFFU);
    trapsignal(current_process, sig, code);
    return 1;
}

/* Is the kernel's own BIOS call what runs in VM86 mode? */
int vm86_kernel_bios_active(void) {
    return current_vm86_monitor && current_vm86_monitor->in_kernel_bios;
}

/*
 * A trap other than #GP (13) taken in VM86 mode: an invalid opcode, a
 * divide error, a fault on memory nothing maps.  The kernel's BIOS call
 * ends, its caller seeing a failed call; a user guest is ended by the
 * signal, as vm86_fail() ends it for an instruction the monitor cannot
 * carry out -- neither panics the kernel, as a VM86 trap is no kernel-mode
 * fault.  Returns 1 if a signal was posted.
 */
int vm86_trap(registers_t *regs) {
    int sig = SIGSEGV;
    int code = SEGV_MAPERR;

    switch (regs->int_no) {
    case 0:
        sig = SIGFPE;
        code = FPE_INTDIV;
        break;
    case 6:
        sig = SIGILL;
        code = ILL_ILLOPC;
        break;
    case 16:
        sig = SIGFPE;
        code = 0;
        break;
    case 1:
    case 3:
        sig = SIGTRAP;
        code = 0;
        break;
    default:
        break;
    }
    return vm86_fail(regs, regs->int_no, sig, code);
}

int vm86_gpf_handler(registers_t *regs) {
    // We trapped here because of a sensitive instruction in VM86 mode (INT n, CLI, STI, IN/OUT)
    // We need to emulate it or reflect it to the monitor process.

    // 1. Fetch instruction byte(s) from CS:IP
    // Need linear address: (CS << 4) + EIP
    uint32_t cs = regs->cs & 0xFFFF;
    uint32_t ip = regs->eip & 0xFFFF;
    uint32_t linear = (cs << 4) + ip;

    uint8_t code[2] = { 0, 0 };
    if (vm86_read(linear, code, 1) != 0) {
        return vm86_fail(regs, 0xFFFFFFFFU, SIGSEGV, SEGV_MAPERR);
    }
    uint8_t opcode = code[0];
    /* The opcodes that take an imm8 (INT n, IN/OUT imm8) need the second
     * byte; only they read it, so a one-byte opcode at the end of the
     * guest's memory is not refused for the byte after it. */
    if ((opcode == 0xCD || opcode == 0xE4 || opcode == 0xE6) &&
        vm86_read(linear + 1, &code[1], 1) != 0) {
        return vm86_fail(regs, opcode, SIGSEGV, SEGV_MAPERR);
    }

    if (opcode == 0xCD) { // INT n
        uint8_t int_no = code[1];
        // Emulate interrupt:
        // Push FLAGS, CS, IP to VM86 Stack
        // Jump to IVT[n]

        uint16_t ss = regs->ss;
        uint16_t sp = regs->useresp;
        uint32_t stack_linear = (ss << 4) + sp;
        uint16_t frame[3];
        uint16_t vec[2];

        frame[0] = (uint16_t)(regs->eip + 2); /* Return after INT n */
        frame[1] = (uint16_t)regs->cs;
        frame[2] = vm86_flags_image(regs->eflags);

        // New CS:IP from the IVT (at 0x0000)
        if (vm86_read(int_no * 4U, vec, sizeof(vec)) != 0 ||
            vm86_write(stack_linear - 6U, frame, sizeof(frame)) != 0) {
            return vm86_fail(regs, opcode, SIGSEGV, SEGV_MAPERR);
        }

        regs->useresp = (uint32_t)(uint16_t)(sp - 6);
        regs->eip = vec[0];
        regs->cs  = vec[1];

        // Clear the guest's IF (virtual) and TF; the real IF stays set
        regs->eflags = (regs->eflags & ~(VM86_EFLAGS_VIF | 0x100U)) | VM86_EFLAGS_IF;
        return 0;
    }

    if (opcode == 0xFA) { // CLI: the guest's IF, VIF, not the CPU's
        regs->eflags = (regs->eflags & ~VM86_EFLAGS_VIF) | VM86_EFLAGS_IF;
        regs->eip += 1;
        return 0;
    }

    if (opcode == 0xFB) { // STI
        regs->eflags |= VM86_EFLAGS_VIF | VM86_EFLAGS_IF;
        regs->eip += 1;
        return 0;
    }

    if (opcode == 0x9C) { // PUSHF - Push FLAGS register
        uint16_t ss = regs->ss;
        uint16_t sp = regs->useresp;
        uint32_t stack_linear = (ss << 4) + sp;
        uint16_t flags = vm86_flags_image(regs->eflags);

        if (vm86_write(stack_linear - 2U, &flags, sizeof(flags)) != 0) {
            return vm86_fail(regs, opcode, SIGSEGV, SEGV_MAPERR);
        }

        regs->useresp = (uint16_t)(sp - 2);
        regs->eip += 1;
        return 0;
    }

    if (opcode == 0x9D) { // POPF - Pop FLAGS register
        uint16_t ss = regs->ss;
        uint16_t sp = regs->useresp;
        uint32_t stack_linear = (ss << 4) + sp;
        uint16_t flags;

        if (vm86_read(stack_linear, &flags, sizeof(flags)) != 0) {
            return vm86_fail(regs, opcode, SIGSEGV, SEGV_MAPERR);
        }

        /* Preserve VM, IOPL, NT and the other privileged bits; IF is VIF */
        regs->eflags = vm86_flags_take(regs->eflags, flags, 0xFFFE0000U | VM86_EFLAGS_PRIV);

        regs->useresp = (uint16_t)(sp + 2);
        regs->eip += 1;
        return 0;
    }

    if (opcode == 0xCF) { // IRET
        uint16_t ss = regs->ss;
        uint16_t sp = regs->useresp;
        uint16_t frame[3];

        if (vm86_read((ss << 4) + sp, frame, sizeof(frame)) != 0) {
            return vm86_fail(regs, opcode, SIGSEGV, SEGV_MAPERR);
        }

        regs->eip = frame[0];
        regs->cs = frame[1];
        /* As POPF: the guest's flags image does not get to set IOPL or NT */
        regs->eflags = vm86_flags_take(regs->eflags, frame[2], 0xFFFF0000U | VM86_EFLAGS_PRIV);
        regs->useresp = (uint16_t)(sp + 6);
        return 0;
    }

    /*
     * IN and OUT, of AL, AX or (with an operand-size prefix) EAX, at an
     * imm8 port or DX's: E4-E7 and EC-EF.  A video BIOS writes its index
     * registers a word at a time.
     */
    if (opcode == 0x66 || (opcode & 0xF4U) == 0xE4U) {
        uint8_t op = opcode;
        uint32_t len = 1;
        int wide = 0;
        uint32_t size;
        uint16_t port;

        if (op == 0x66) {
            if (vm86_read(linear + 1, &op, 1) != 0) {
                return vm86_fail(regs, opcode, SIGSEGV, SEGV_MAPERR);
            }
            wide = 1;
            len = 2;
            if ((op & 0xF4U) != 0xE4U) {
                return vm86_fail(regs, opcode, SIGILL, ILL_PRVOPC);
            }
        }
        size = (op & 1U) ? (wide ? 4U : 2U) : 1U;
        if ((op & 0x08U) == 0) {    /* E4-E7: the port is an imm8 */
            uint8_t imm;

            if (vm86_read(linear + len, &imm, 1) != 0) {
                return vm86_fail(regs, opcode, SIGSEGV, SEGV_MAPERR);
            }
            port = imm;
            len++;
        } else {
            port = (uint16_t)(regs->edx & 0xFFFFU);
        }
        if (op & 0x02U) {
            vm86_port_out(port, size, regs->eax);
        } else {
            uint32_t mask = size == 4 ? 0xFFFFFFFFU : (1U << (size * 8)) - 1U;

            regs->eax = (regs->eax & ~mask) | (vm86_port_in(port, size) & mask);
        }
        regs->eip += len;
        return 0;
    }
    
    /* Handle HLT (0xF4) as exit from VM86 BIOS callback */
    if (opcode == 0xF4) {
        /* Check if we are in kernel-mode BIOS call */
        if (current_vm86_monitor && current_vm86_monitor->in_kernel_bios) {
            /* the stub's INT n is done: back to vm86_bios_call() */
            vm86_kernel_bios_exit(regs);
            return 0;
        }

        kprint("VM86: HLT opcode in user VM86\n");
        return vm86_fail(regs, opcode, SIGILL, ILL_PRVOPC);
    }

    kprintf("VM86: Unhandled opcode in GPF (CS:IP %04X:%04X Op %02X)\n",
           (unsigned)regs->cs, (unsigned)regs->eip, (unsigned)opcode);
    return vm86_fail(regs, opcode, SIGILL, ILL_PRVOPC);
}

/* Kernel BIOS Call Helper */
int vm86_bios_call(int int_no, struct vm86_regs *regs) {
    /* 1. Identity Map first 1MB (if not already) - Assumed done/safe? */
    /* Substrate Higher Half unmaps 0-4MB. We MUST remap it. */
    /* We can use 4MB pages or 4KB. */
    /* Let's assume we can temporarirly map it. */
    /* Map 0->0, 4K->4K ... up to 1MB. */
    pmap_t kpmap = pmap_kernel();
    for (uint32_t i = 0; i < 0x100000; i += 0x1000) {
        pmap_enter(kpmap, i, i, VM_PROT_ALL, 0); // User accessible for VM86
    }
    
    /* 2. Setup Monitor State */
    struct vm86_monitor mon;
    vm86_monitor_init(&mon);
    mon.in_kernel_bios = 1;
    mon.out_regs = regs;
    
    /* 3. Setup VM86 Struct */
    struct vm86_struct info;
    memset(&info, 0, sizeof(info));
    if (regs) info.regs = *regs;

    /* Setup Stack at 0x7C00 (safest area usually) or 0x1000 */
    /* We need to put our stub code somewhere too. 0x8000? */
    uint32_t stack_base = 0x2000;
    uint32_t code_base  = 0x1000;
    
    info.regs.esp = stack_base;
    info.regs.ss  = 0x0000;
    info.regs.cs  = 0x0000;
    info.regs.eip = code_base;
    
    /* 4. Write Stub Code: INT n; HLT */
    uint8_t *stub = vm86_linear_ptr(code_base, 3); // Identity mapped
    if (!stub) {
        current_vm86_monitor = NULL;
        return -EFAULT;
    }
    stub[0] = 0xCD;       // INT
    stub[1] = int_no;     // imm8
    stub[2] = 0xF4;       // HLT
    
    /* 5. Enter VM86: by the kernel's entry, as &info is no user address.
     * It returns when HLT is hit, via vm86_bios_ret_point, on the stack
     * vm86_enter made the TSS's: no switch to another thread may move it
     * meanwhile, as the scheduler would set the TSS for that thread. */
    preempt_disable();
    kern_vm86(&info);
    preempt_enable_noresched();
    /* mon is this frame's: do not leave it current once the call is over,
     * or a later user guest would be taken for a kernel BIOS call. */
    current_vm86_monitor = NULL;
    
    /* 6. Copy back registers */
    /*
     * The registers are copied back to `regs` in vm86_gpf_handler
     * when the HLT opcode is encountered.
     */
    
    return 0;
}
