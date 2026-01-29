#include <stdint.h>
#include "include/early_boot.h"

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

/* Current exception number - set by stubs */
volatile int early_exception_num = -1;

/* Early exception handler - prints via UART and halts */
void early_exception_handler(void) {
    early_uart_print("!!! EARLY EXCEPTION #");
    early_print_hex(early_exception_num);
    early_uart_print(" !!!\n");
    
    /* Try to get some useful info from stack */
    uint32_t eip;
    __asm__ volatile("mov 4(%%ebp), %0" : "=r"(eip));
    early_uart_print("EIP: ");
    early_print_hex(eip);
    early_uart_print("\n");
    
    /* Halt forever */
    for (;;) __asm__ volatile("hlt");
}

/* Macro to generate exception stubs */
#define EARLY_ISR(n) \
    __attribute__((naked)) void early_isr##n(void) { \
        __asm__ volatile( \
            "pusha\n" \
            "movl $" #n ", %0\n" \
            "call early_exception_handler\n" \
            "popa\n" \
            "iret\n" \
            : "=m"(early_exception_num) \
        ); \
    }

EARLY_ISR(0)  EARLY_ISR(1)  EARLY_ISR(2)  EARLY_ISR(3)
EARLY_ISR(4)  EARLY_ISR(5)  EARLY_ISR(6)  EARLY_ISR(7)
EARLY_ISR(8)  EARLY_ISR(9)  EARLY_ISR(10) EARLY_ISR(11)
EARLY_ISR(12) EARLY_ISR(13) EARLY_ISR(14) EARLY_ISR(15)
EARLY_ISR(16) EARLY_ISR(17) EARLY_ISR(18) EARLY_ISR(19)
EARLY_ISR(20) EARLY_ISR(21) EARLY_ISR(22) EARLY_ISR(23)
EARLY_ISR(24) EARLY_ISR(25) EARLY_ISR(26) EARLY_ISR(27)
EARLY_ISR(28) EARLY_ISR(29) EARLY_ISR(30) EARLY_ISR(31)

/* Array of handler pointers */
static void (*early_isr_table[32])(void) = {
    early_isr0,  early_isr1,  early_isr2,  early_isr3,
    early_isr4,  early_isr5,  early_isr6,  early_isr7,
    early_isr8,  early_isr9,  early_isr10, early_isr11,
    early_isr12, early_isr13, early_isr14, early_isr15,
    early_isr16, early_isr17, early_isr18, early_isr19,
    early_isr20, early_isr21, early_isr22, early_isr23,
    early_isr24, early_isr25, early_isr26, early_isr27,
    early_isr28, early_isr29, early_isr30, early_isr31
};

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
        early_idt_set_gate(i, (uint32_t)early_isr_table[i]);
    }
    
    /* Load the IDT */
    __asm__ volatile("lidt %0" : : "m"(early_idt_ptr));
}
