#include <stdint.h>
#include <arch/i386/early_boot.h>

/* Hex digit lookup */
static const char hex_digits[] = "0123456789ABCDEF";

static void early_uart_putc(char c) {
    uint16_t port = 0x3F8;
    // Wait for transmit buffer empty
    uint8_t status;
    do {
        __asm__ volatile("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)(port + 5)));
    } while ((status & 0x20) == 0);
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)c), "Nd"(port));
}

void early_uart_print(const char *s) {
    while (*s) early_uart_putc(*s++);
}

static void early_print_hex(uint32_t val) {
    char buf[11] = "0x00000000";
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex_digits[val & 0xF];
        val >>= 4;
    }
    early_uart_print(buf);
}

/* Current exception number (for compatibility with previous diagnostics) */
volatile int early_exception_num = 0;

/* Early exception handler - prints fault context via UART and halts */
void early_exception_handler(uint32_t vec, uint32_t err, uint32_t eip,
                             uint32_t cs, uint32_t eflags, uint32_t cr2) {
    early_exception_num = (int)vec;

    early_uart_print("!!! EARLY EXCEPTION #");
    early_print_hex(vec);
    early_uart_print(" !!!\n");

    early_uart_print("ERR: ");
    early_print_hex(err);
    early_uart_print("\n");

    early_uart_print("CR2: ");
    early_print_hex(cr2);
    early_uart_print("\n");

    early_uart_print("EIP: ");
    early_print_hex(eip);
    early_uart_print("\n");

    early_uart_print("CS: ");
    early_print_hex(cs);
    early_uart_print("\n");

    early_uart_print("EFLAGS: ");
    early_print_hex(eflags);
    early_uart_print("\n");

    for (;;) __asm__ volatile("hlt");
}

__attribute__((used, naked)) void early_isr_common(void) {
    __asm__ volatile(
        "pusha\n"
        "movl %esp, %ebx\n"       /* Base of saved register frame */
        "movl %cr2, %eax\n"
        "pushl %eax\n"            /* arg6: cr2 */
        "pushl 48(%ebx)\n"        /* arg5: eflags */
        "pushl 44(%ebx)\n"        /* arg4: cs */
        "pushl 40(%ebx)\n"        /* arg3: eip */
        "pushl 36(%ebx)\n"        /* arg2: err */
        "pushl 32(%ebx)\n"        /* arg1: vec */
        "call early_exception_handler\n"
        "add $24, %esp\n"
        "1:\n"
        "hlt\n"
        "jmp 1b\n"
    );
}

#define EARLY_ISR_NOERR(idx) \
    __attribute__((naked)) void early_isr##idx(void) { \
        __asm__ volatile( \
            "pushl $0\n" \
            "pushl $" #idx "\n" \
            "jmp early_isr_common\n" \
        ); \
    }

#define EARLY_ISR_ERR(idx) \
    __attribute__((naked)) void early_isr##idx(void) { \
        __asm__ volatile( \
            "pushl $" #idx "\n" \
            "jmp early_isr_common\n" \
        ); \
    }

EARLY_ISR_NOERR(0)  EARLY_ISR_NOERR(1)  EARLY_ISR_NOERR(2)  EARLY_ISR_NOERR(3)
EARLY_ISR_NOERR(4)  EARLY_ISR_NOERR(5)  EARLY_ISR_NOERR(6)  EARLY_ISR_NOERR(7)
EARLY_ISR_ERR(8)    EARLY_ISR_NOERR(9)  EARLY_ISR_ERR(10)   EARLY_ISR_ERR(11)
EARLY_ISR_ERR(12)   EARLY_ISR_ERR(13)   EARLY_ISR_ERR(14)   EARLY_ISR_NOERR(15)
EARLY_ISR_NOERR(16) EARLY_ISR_ERR(17)   EARLY_ISR_NOERR(18) EARLY_ISR_NOERR(19)
EARLY_ISR_NOERR(20) EARLY_ISR_ERR(21)   EARLY_ISR_NOERR(22) EARLY_ISR_NOERR(23)
EARLY_ISR_NOERR(24) EARLY_ISR_NOERR(25) EARLY_ISR_NOERR(26) EARLY_ISR_NOERR(27)
EARLY_ISR_NOERR(28) EARLY_ISR_NOERR(29) EARLY_ISR_NOERR(30) EARLY_ISR_NOERR(31)

static uint32_t early_isr_addr(int n) {
    switch(n) {
    case 0: return (uint32_t)(uintptr_t)early_isr0;
    case 1: return (uint32_t)(uintptr_t)early_isr1;
    case 2: return (uint32_t)(uintptr_t)early_isr2;
    case 3: return (uint32_t)(uintptr_t)early_isr3;
    case 4: return (uint32_t)(uintptr_t)early_isr4;
    case 5: return (uint32_t)(uintptr_t)early_isr5;
    case 6: return (uint32_t)(uintptr_t)early_isr6;
    case 7: return (uint32_t)(uintptr_t)early_isr7;
    case 8: return (uint32_t)(uintptr_t)early_isr8;
    case 9: return (uint32_t)(uintptr_t)early_isr9;
    case 10: return (uint32_t)(uintptr_t)early_isr10;
    case 11: return (uint32_t)(uintptr_t)early_isr11;
    case 12: return (uint32_t)(uintptr_t)early_isr12;
    case 13: return (uint32_t)(uintptr_t)early_isr13;
    case 14: return (uint32_t)(uintptr_t)early_isr14;
    case 15: return (uint32_t)(uintptr_t)early_isr15;
    case 16: return (uint32_t)(uintptr_t)early_isr16;
    case 17: return (uint32_t)(uintptr_t)early_isr17;
    case 18: return (uint32_t)(uintptr_t)early_isr18;
    case 19: return (uint32_t)(uintptr_t)early_isr19;
    case 20: return (uint32_t)(uintptr_t)early_isr20;
    case 21: return (uint32_t)(uintptr_t)early_isr21;
    case 22: return (uint32_t)(uintptr_t)early_isr22;
    case 23: return (uint32_t)(uintptr_t)early_isr23;
    case 24: return (uint32_t)(uintptr_t)early_isr24;
    case 25: return (uint32_t)(uintptr_t)early_isr25;
    case 26: return (uint32_t)(uintptr_t)early_isr26;
    case 27: return (uint32_t)(uintptr_t)early_isr27;
    case 28: return (uint32_t)(uintptr_t)early_isr28;
    case 29: return (uint32_t)(uintptr_t)early_isr29;
    case 30: return (uint32_t)(uintptr_t)early_isr30;
    case 31: return (uint32_t)(uintptr_t)early_isr31;
    }
    return (uint32_t)(uintptr_t)early_isr0;
}

/* Early GDT for early exceptions */
struct early_gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct early_gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct early_gdt_entry early_gdt[3];
static struct early_gdt_ptr early_gdt_ptr;

/* Early IDT for debugging boot faults */
struct early_idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct early_idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct early_idt_entry early_idt[32];
static struct early_idt_ptr early_idt_ptr;

void early_gdt_init(void) {
    /* Entry 0: Null descriptor */
    early_gdt[0].limit_low = 0;
    early_gdt[0].base_low = 0;
    early_gdt[0].base_mid = 0;
    early_gdt[0].access = 0;
    early_gdt[0].granularity = 0;
    early_gdt[0].base_high = 0;
    
    /* Entry 1 (0x08): Code segment - base 0, limit 4GB, execute/read */
    early_gdt[1].limit_low = 0xFFFF;
    early_gdt[1].base_low = 0;
    early_gdt[1].base_mid = 0;
    early_gdt[1].access = 0x9A;      /* Present, ring 0, code, execute/read */
    early_gdt[1].granularity = 0xCF; /* 4KB granularity, 32-bit */
    early_gdt[1].base_high = 0;
    
    /* Entry 2 (0x10): Data segment - base 0, limit 4GB, read/write */
    early_gdt[2].limit_low = 0xFFFF;
    early_gdt[2].base_low = 0;
    early_gdt[2].base_mid = 0;
    early_gdt[2].access = 0x92;      /* Present, ring 0, data, read/write */
    early_gdt[2].granularity = 0xCF; /* 4KB granularity, 32-bit */
    early_gdt[2].base_high = 0;
    
    early_gdt_ptr.limit = sizeof(early_gdt) - 1;
    early_gdt_ptr.base = (uint32_t)&early_gdt;
    
    /* Load GDT and reload segment registers */
    __asm__ volatile(
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "m"(early_gdt_ptr) : "eax"
    );
}

static void early_idt_set_gate(int n, uint32_t handler) {
    early_idt[n].offset_low = handler & 0xFFFF;
    early_idt[n].selector = 0x08;  /* Kernel code segment */
    early_idt[n].zero = 0;
    early_idt[n].type_attr = 0x8E; /* Present, ring 0, 32-bit interrupt gate */
    early_idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

void early_idt_init(void) {
    early_idt_ptr.limit = sizeof(early_idt) - 1;
    early_idt_ptr.base = (uint32_t)&early_idt;
    
    /* Set all 32 exception vectors to our early handlers */
    for (int i = 0; i < 32; i++) {
        early_idt_set_gate(i, early_isr_addr(i));
    }
    
    /* Load the IDT */
    __asm__ volatile("lidt %0" : : "m"(early_idt_ptr));
}
