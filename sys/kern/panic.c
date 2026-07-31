#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/i386/idt.h>
#include <arch/x86-common/lapic.h>
#include <drivers/console/uart/uart.h>
#include <drivers/video/fb.h>
#include <drivers/video/hw_text.h>
#include <drivers/video/vga.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <kern/stacktrace.h>
#include <sys/copy.h>

#ifdef HOST_TEST
void panic_test_halt(void);
#endif

#define KERN_BASE 0xC0000000U

/*
 * Reentrancy guard.  panic() and panic_with_regs() route through
 * kprint -> console -> tty/vt -> framebuffer/ANSI handler, any of
 * which can themselves panic.  Without a guard, the second panic
 * starts a third, etc., until the screen is just header swarms with
 * no body — the symptom we observed running `cat /dev/urandom`.
 *
 * On the second entry we abandon the rich console/VT path entirely
 * and emit raw text directly to the COM port via uart_panic_write,
 * which holds no locks and uses polled THR-empty.  After two levels
 * of recursion we simply halt.
 *
 * [USB-HW-03] This used to be the WHOLE story, and `int d = ++panic_depth`
 * on a plain volatile int is not an atomic read-modify-write: two CPUs
 * faulting at the same moment both read 0, both compute 1, and both take
 * the full rich-console path.  Their output then interleaves character by
 * character, which is exactly the unreadable doubled exception dump
 * photographed on real hardware.  Even with the increment made atomic the
 * guard would still be wrong for SMP, because it counts RECURSION and
 * cannot tell "this CPU faulted again while dumping" (where serial-only is
 * right) from "a different CPU also panicked" (where the second dump must
 * simply wait its turn).
 *
 * So ownership is now explicit.  The first CPU to arrive claims panic_owner
 * with a compare-and-swap and prints the rich dump; panic_depth is its
 * private recursion counter.  Any other CPU waits for the owner to finish
 * and then emits a compact serial-only dump of its own, so a genuine
 * multi-CPU fault yields two readable dumps in sequence instead of two
 * shredded ones on top of each other.
 */
#define PANIC_NO_OWNER  (-1)

static volatile int panic_owner = PANIC_NO_OWNER;
static volatile int panic_depth = 0;
static volatile int panic_dump_done = 0;

#ifdef HOST_TEST
/* The host test drives this to exercise the owner/secondary/recursive
 * arbitration without needing real CPUs. */
extern int panic_test_cpu_id;
#endif

static int panic_cpu_id(void) {
#ifdef HOST_TEST
    return panic_test_cpu_id;
#else
    /* Before the LAPIC is up there is only one CPU running, so 0 is right
     * and lapic_get_id() would be reading an unmapped MMIO window. */
    return lapic_is_initialized() ? (int)lapic_get_id() : 0;
#endif
}

static void panic_serial_emit(const char *s) {
    if (!s) return;
    size_t n = 0;
    while (s[n]) n++;
    uart_panic_write(s, n);
}

static void panic_serial_hex32(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> (28 - i * 4)) & 0xF];
    }
    buf[10] = '\0';
    panic_serial_emit(buf);
}

/*
 * [USB-HW-03 lead (c)] Is every page of [va, va+len) present in the CURRENT
 * address space?
 *
 * The dump helpers below used to gate their raw reads on a pure RANGE check
 * (see panic_addr_is_kernel_text).  A range check is not a mapped-ness check:
 * a kernel virtual address inside [KERN_BASE, 0xFF000000) that has no page
 * table entry faults the instant we touch it -- and we are already inside a
 * fault handler, so that is a re-entrant fault taken while printing the dump
 * of the first one.  This matters precisely for the corrupted-register case
 * we are trying to diagnose: the hardware report has ebp/ds = 0x1B232B00,
 * i.e. exactly the garbage-pointer situation where the dump would fault.
 *
 * The page directory is walked straight off CR3 rather than through pmap_*
 * so this depends on no kernel state beyond the hardware's own tables, which
 * is the only thing we can still trust here.
 */
static int panic_va_readable(uintptr_t va, size_t len) {
#ifdef HOST_TEST
    (void)va; (void)len;
    return 0;
#else
    uint32_t cr3;
    uintptr_t page, last;

    if (len == 0)
        return 0;

    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    /* The page directory itself must be reachable through the direct map. */
    if ((cr3 & 0xFFFFF000U) >= (0xFFFFFFFFU - KERN_BASE))
        return 0;

    uint32_t *pd = (uint32_t *)((cr3 & 0xFFFFF000U) + KERN_BASE);

    last = va + (len - 1);
    if (last < va)                    /* wrapped */
        return 0;

    for (page = va & ~0xFFFU; page <= (last & ~0xFFFU); page += 0x1000) {
        uint32_t pde = pd[page >> 22];

        if (!(pde & 0x1))             /* not present */
            return 0;
        if (pde & 0x80)               /* 4 MiB page: present, no page table */
            continue;

        if ((pde & 0xFFFFF000U) >= (0xFFFFFFFFU - KERN_BASE))
            return 0;
        uint32_t *pt = (uint32_t *)((pde & 0xFFFFF000U) + KERN_BASE);
        if (!(pt[(page >> 12) & 0x3FF] & 0x1))
            return 0;
    }
    return 1;
#endif
}

int panic_addr_readable(uintptr_t va, size_t len) {
    return panic_va_readable(va, len);
}

static int panic_addr_is_kernel_text(uintptr_t va) {
#ifdef HOST_TEST
    /* Host unit tests have no kernel address space; never treat a host
     * pointer as directly-readable kernel memory.  The raw memcpy() guarded
     * by this predicate would otherwise fault intermittently on a garbage
     * trap-frame esp that happens to land in the kernel range. */
    (void)va;
    return 0;
#else
    /* Conservative: kernel virtual addresses live in [KERN_BASE, 0xFF000000). */
    return va >= KERN_BASE && va < 0xFF000000U;
#endif
}

static void panic_dump_bytes_at(uintptr_t va, int is_user) {
    unsigned char buf[16];
    char line[128];

    if (va == 0) {
        kprint("CODE @eip: <null>\n");
        return;
    }

    if (is_user) {
        if (copyin((const void *)va, buf, sizeof(buf)) != 0) {
            kprint("CODE @eip: <user not mapped>\n");
            return;
        }
    } else if (panic_addr_is_kernel_text(va) &&
               panic_va_readable(va, sizeof(buf))) {
        memcpy(buf, (const void *)va, sizeof(buf));
    } else {
        kprint("CODE @eip: <unsafe to read>\n");
        return;
    }

    snprintf(line, sizeof(line),
            "CODE @eip: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
            buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
    kprint(line);
}

static void panic_dump_stack_words(uintptr_t esp, int is_user) {
    uint32_t buf[8];
    char line[160];

    if (esp == 0) return;

    if (is_user) {
        if (copyin((const void *)esp, buf, sizeof(buf)) != 0) {
            kprint("STK: <user not mapped>\n");
            return;
        }
    } else if (panic_addr_is_kernel_text(esp) &&
               panic_va_readable(esp, sizeof(buf))) {
        memcpy(buf, (const void *)esp, sizeof(buf));
    } else {
        kprint("STK: <unsafe to read>\n");
        return;
    }

    snprintf(line, sizeof(line), "STK: +00=%08x +04=%08x +08=%08x +0c=%08x\n",
            buf[0], buf[1], buf[2], buf[3]);
    kprint(line);
    snprintf(line, sizeof(line), "STK: +10=%08x +14=%08x +18=%08x +1c=%08x\n",
            buf[4], buf[5], buf[6], buf[7]);
    kprint(line);
}

static void panic_dump_regs(const registers_t *regs) {
    char line[160];
    int is_user;
    uintptr_t fault_esp;

    if (!regs) return;

    is_user = (regs->cs & 0x3) != 0;
    /* For ring transitions the trap frame has useresp; for kernel-mode
     * traps the saved esp is the kernel stack pointer (regs->esp from pusha). */
    fault_esp = is_user ? regs->useresp : regs->esp;

    snprintf(line, sizeof(line),
            "REGS eip=%08x cs=%04x eflags=%08x esp=%08x ss=%04x int=%u err=%08x\n",
            regs->eip, (unsigned)(regs->cs & 0xFFFF), regs->eflags,
            fault_esp, (unsigned)(regs->ss & 0xFFFF),
            (unsigned)regs->int_no, regs->err_code);
    kprint(line);
    snprintf(line, sizeof(line), "REGS eax=%08x ebx=%08x ecx=%08x edx=%08x\n",
            regs->eax, regs->ebx, regs->ecx, regs->edx);
    kprint(line);
    snprintf(line, sizeof(line), "REGS esi=%08x edi=%08x ebp=%08x ds=%04x es=%04x fs=%04x gs=%04x\n",
            regs->esi, regs->edi, regs->ebp,
            (unsigned)(regs->ds & 0xFFFF), (unsigned)(regs->es & 0xFFFF),
            (unsigned)(regs->fs & 0xFFFF), (unsigned)(regs->gs & 0xFFFF));
    kprint(line);

    panic_dump_bytes_at(regs->eip, is_user);
    panic_dump_stack_words(fault_esp, is_user);
}

static void panic_stop_other_cpus(void) {
#ifndef HOST_TEST
    /* Send PANIC_IPI to every CPU except this one.  Each remote CPU's
     * isr_panic_ipi stub does an immediate cli;hlt;jmp$, so they stop
     * scheduling user processes and stay halted while we print the
     * panic message.  Without this an AP would happily keep running
     * init / playing MP3s / accepting VT logins after the BSP
     * crashed. */
    if (lapic_is_initialized()) {
        lapic_send_ipi_all_excl_self(0xFB /* PANIC_IPI_VECTOR */);
    }
#endif
}

static void panic_emit_header(const char *msg) {
#ifndef HOST_TEST
    /* Disable interrupts immediately, then halt every other CPU
     * BEFORE we start any printing — otherwise an AP would still
     * be running while we're trying to render the red header. */
    __asm__ volatile("cli");
    panic_stop_other_cpus();
#endif

    /* Set high-visibility color but DON'T clear screen */
    if (!fb_active) {
        hw_text_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    }

    if (msg) {
        kprint("\n\n*** KERNEL PANIC ***\n");
        kprint("Fatal Error: ");
        kprint(msg);
        kprint("\n");
    } else {
        kprint("\n\n*** KERNEL PANIC ***\n");
        kprint("Fatal Error: Unknown\n");
    }
}

static void panic_finish(void) {
    /* Print stack trace */
    stack_trace();

    kprint("\nSystem Halted.\n");

    /* Release any CPU waiting to print its own dump after ours.  This must
     * happen after the last kprint and before we halt, or a concurrent
     * panic on another CPU either interleaves with us (too early) or is
     * never printed at all (too late — we never leave the halt loop). */
    panic_dump_done = 1;
    __sync_synchronize();

#ifdef HOST_TEST
    panic_test_halt();
    return;
#endif

    /* Halt */
    while(1) {
        __asm__ volatile("hlt");
    }
}

/*
 * Recursive-panic emergency path: write a tiny notice plus the message
 * straight to the COM port, then halt.  Bypasses kprint / console /
 * VT / TTY / framebuffer entirely so we cannot recurse again.
 */
static void panic_serial_only(const char *msg, const registers_t *regs) {
#ifndef HOST_TEST
    __asm__ volatile("cli");
    panic_stop_other_cpus();
#endif
    panic_serial_emit("\n\n*** RECURSIVE KERNEL PANIC ***\n");
    panic_serial_emit("Fatal Error: ");
    panic_serial_emit(msg ? msg : "(null)");
    panic_serial_emit("\n");
    if (regs) {
        panic_serial_emit("eip=");  panic_serial_hex32(regs->eip);
        panic_serial_emit(" esp=");
        panic_serial_hex32((regs->cs & 0x3) ? regs->useresp : regs->esp);
        panic_serial_emit(" eflags=");
        panic_serial_hex32(regs->eflags);
        panic_serial_emit("\n");
    }
    panic_serial_emit("System Halted (recursive).\n");
#ifdef HOST_TEST
    panic_test_halt();
    return;
#endif
    while (1) { __asm__ volatile("hlt"); }
}

/*
 * A second CPU panicked while the first was still printing.  Wait for the
 * owner to finish its dump, then emit ours straight to the COM port.
 *
 * The wait is bounded: if the owner wedges part-way through its dump we
 * would rather append a slightly-interleaved second dump than print
 * nothing at all, because on a two-CPU fault the second CPU's registers
 * are frequently the interesting ones.
 */
static void panic_secondary(int cpu, const char *msg, const registers_t *regs) {
#ifndef HOST_TEST
    __asm__ volatile("cli");

    for (uint32_t spins = 0; spins < 400000000u; spins++) {
        if (panic_dump_done)
            break;
        __asm__ volatile("pause");
    }
#endif

    panic_serial_emit("\n*** KERNEL PANIC on CPU ");
    panic_serial_hex32((uint32_t)cpu);
    panic_serial_emit(" (concurrent with the dump above) ***\n");
    panic_serial_emit("Fatal Error: ");
    panic_serial_emit(msg ? msg : "(null)");
    panic_serial_emit("\n");
    if (regs) {
        panic_serial_emit("eip=");  panic_serial_hex32(regs->eip);
        panic_serial_emit(" esp=");
        panic_serial_hex32((regs->cs & 0x3) ? regs->useresp : regs->esp);
        panic_serial_emit(" eflags="); panic_serial_hex32(regs->eflags);
        panic_serial_emit("\n cs=");  panic_serial_hex32(regs->cs);
        panic_serial_emit(" ds=");    panic_serial_hex32(regs->ds);
        panic_serial_emit(" err=");   panic_serial_hex32(regs->err_code);
        panic_serial_emit("\n");
    }
    panic_serial_emit("System Halted (secondary CPU).\n");

#ifdef HOST_TEST
    panic_test_halt();
    return;
#endif
    while (1) { __asm__ volatile("hlt"); }
}

/*
 * Decide this CPU's role in the panic.  Returns 1 if we own the dump and
 * should take the rich console path, 0 if we already handled it (recursive
 * or secondary) and the caller must return immediately.
 */
static int panic_enter(const char *msg, const registers_t *regs) {
    int cpu = panic_cpu_id();

    if (__sync_bool_compare_and_swap(&panic_owner, PANIC_NO_OWNER, cpu)) {
        panic_depth = 1;
        return 1;                       /* we own it: print the full dump */
    }

    if (panic_owner == cpu) {
        /* Same CPU faulted again inside its own dump — the console/VT path
         * is the usual culprit, so drop to the lock-free serial emitter. */
        if (++panic_depth > 8) {
            /* Wedged in recursion; stop emitting entirely. */
#ifndef HOST_TEST
            __asm__ volatile("cli");
            while (1) { __asm__ volatile("hlt"); }
#else
            panic_test_halt();
            return 0;
#endif
        }
        panic_serial_only(msg, regs);
        return 0;
    }

    panic_secondary(cpu, msg, regs);
    return 0;
}

void panic_with_regs(const char *msg, const registers_t *regs) {
    if (!panic_enter(msg, regs))
        return;
    panic_emit_header(msg);
    if (regs) {
        panic_dump_regs(regs);
    }
    panic_finish();
}

void panic(const char *msg) {
    if (!panic_enter(msg, NULL))
        return;
    panic_emit_header(msg);
    /* No trap frame available; synthesize the call site so the operator at
     * least sees where panic() was invoked from and a peek at the kernel
     * stack and instruction stream there. */
    {
        registers_t fake;
        memset(&fake, 0, sizeof(fake));
        fake.cs = 0x08; /* kernel cs (ring 0) */
        fake.ss = 0x10;
        fake.ds = fake.es = fake.fs = fake.gs = 0x10;
        fake.eip = (uint32_t)(uintptr_t)__builtin_return_address(0);
        fake.ebp = (uint32_t)(uintptr_t)__builtin_frame_address(0);
        __asm__ volatile("mov %%esp, %0" : "=r"(fake.esp));
        fake.useresp = fake.esp;
        panic_dump_regs(&fake);
    }
    panic_finish();
}
