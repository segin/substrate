/*
 * idt.h - x86_64 interrupt descriptor table and trap frame
 *
 * The interface matches arch/i386/idt.h, so the machine-independent trap,
 * signal, ptrace and personality code works on either kernel.
 */

#ifndef _ARCH_X86_64_IDT_H
#define _ARCH_X86_64_IDT_H

#include <stdint.h>

/* IDT entry: 16 bytes in long mode. */
struct idt_entry_struct {
    uint16_t base_low;      /* handler bits 0-15 */
    uint16_t sel;           /* code segment selector */
    uint8_t  ist;           /* IST index (bits 0-2) */
    uint8_t  flags;         /* type and attributes */
    uint16_t base_mid;      /* handler bits 16-31 */
    uint32_t base_high;     /* handler bits 32-63 */
    uint32_t reserved;
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

struct idt_ptr_struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;
extern idt_ptr_t idt_ptr;

/* Type 0xE is the 64-bit interrupt gate in long mode. */
#define IDT_FLAG_PRESENT        0x80
#define IDT_FLAG_DPL3           0x60
#define IDT_FLAG_INT32_GATE     0x0E
#define IDT_FLAG_USER_INT_GATE  (IDT_FLAG_PRESENT | IDT_FLAG_DPL3 | IDT_FLAG_INT32_GATE)

/* Interrupt stack table slots in the TSS (gdt.c). */
#define IST_NONE    0
#define IST_NMI     1
#define IST_DF      2
#define IST_MC      3

void idt_init(void);
void idt_set_gate(uint8_t num, uintptr_t base, uint16_t sel, uint8_t flags);
void idt_set_gate_ist(uint8_t num, uintptr_t base, uint16_t sel, uint8_t flags,
                      uint8_t ist);
void idt_flush(uintptr_t idt_ptr_addr);
const char *idt_exception_name(unsigned int vector);

/* Entry stubs (isr.S). */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

/* Legacy 8259 IRQs 0-15 at vectors 32-47. */
extern void isr32(void); extern void isr33(void); extern void isr34(void);
extern void isr35(void); extern void isr36(void); extern void isr37(void);
extern void isr38(void); extern void isr39(void); extern void isr40(void);
extern void isr41(void); extern void isr42(void); extern void isr43(void);
extern void isr44(void); extern void isr45(void); extern void isr46(void);
extern void isr47(void);

extern void isr128(void);   /* int $0x80: the i386 system-call gate */
extern void isr253(void);   /* SCHED_IPI_VECTOR */
extern void isr254(void);   /* TLB_SHOOTDOWN_VECTOR */
extern void isr_panic_ipi(void);
extern void isr_spurious(void);

/* Entry points for the dynamic (MSI) vector stubs 0x50..0xBF, indexed by
 * (vector - IRQ_VECTOR_FIRST). */
extern void *msi_isr_stubs[];

/*
 * Trap frame, as built by isr.S: lowest address first.
 *
 * Every slot is 64 bits.  The registers a 32-bit (compatibility-mode)
 * process has carry their i386 names as well, overlaying the low half, so
 * code written against arch/i386/idt.h -- regs->eax, regs->eip,
 * regs->useresp -- reads and writes a 32-bit process's registers
 * unchanged.  Writing the 32-bit name leaves the high half alone, which a
 * compatibility-mode process cannot see.  Kernel-mode addresses (a fault
 * inside the kernel, on_fault recovery) need the full 64-bit names; use
 * TF_PC() and TF_SET_PC(), which work on both kernels.
 */
#define TF_REG(r64, r32) union { uint64_t r64; uint32_t r32; }

typedef struct registers {
    TF_REG(gs64, gs);               /* selectors, saved by isr.S */
    TF_REG(fs64, fs);
    TF_REG(es64, es);
    TF_REG(ds64, ds);
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    TF_REG(rdi, edi);               /* the i386 pusha order */
    TF_REG(rsi, esi);
    TF_REG(rbp, ebp);
    TF_REG(rsp_unused, esp);        /* placeholder for pusha's ESP slot */
    TF_REG(rbx, ebx);
    TF_REG(rdx, edx);
    TF_REG(rcx, ecx);
    TF_REG(rax, eax);
    uint64_t int_no, err_code;
    TF_REG(rip, eip);               /* pushed by the processor */
    TF_REG(cs64, cs);
    TF_REG(rflags, eflags);
    TF_REG(rsp, useresp);
    TF_REG(ss64, ss);
} registers_t;

#undef TF_REG

#define TF_PC(r)            ((uintptr_t)(r)->rip)
#define TF_SET_PC(r, pc)    ((r)->rip = (uint64_t)(uintptr_t)(pc))

void isr_handler(registers_t *regs);
void syscall_handler(registers_t *regs);
void signal_handle_pending(registers_t *regs);
int i386_trap_to_signal(const registers_t *regs, uintptr_t cr2, int *sig,
                        int *code, uintptr_t *addr);

#endif /* _ARCH_X86_64_IDT_H */
