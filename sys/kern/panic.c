#include <kern/panic.h>
#include <kern/stacktrace.h>
#include <kern/console.h>
#include <drivers/video/vga.h>
#include <drivers/video/hw_text.h>
#include <drivers/video/fb.h>
#include <drivers/console/uart/uart.h>
#include <arch/i386/idt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Cross-arch LAPIC IPI sender; halts other CPUs by jumping them into
 * isr_panic_ipi (vector PANIC_IPI_VECTOR = 0xFB). */
#ifndef HOST_TEST
extern void lapic_send_ipi_all_excl_self(uint8_t vector);
extern int  lapic_is_initialized(void);
#endif

// Forward decls
void vga_text_set_color(uint8_t fg, uint8_t bg);
extern int copyin(const void *, void *, unsigned int);

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
 * Single global is fine: i386 has no SMP-safe panic protocol here,
 * and once one CPU panics the others are about to be stopped anyway.
 */
static volatile int panic_depth = 0;

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
    } else if (panic_addr_is_kernel_text(va)) {
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
    } else if (panic_addr_is_kernel_text(esp)) {
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

void panic_with_regs(const char *msg, const registers_t *regs) {
    int depth = ++panic_depth;
    if (depth > 1) {
        panic_serial_only(msg, regs);
        return;
    }
    panic_emit_header(msg);
    if (regs) {
        panic_dump_regs(regs);
    }
    panic_finish();
}

void panic(const char *msg) {
    int depth = ++panic_depth;
    if (depth > 1) {
        panic_serial_only(msg, NULL);
        return;
    }
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
