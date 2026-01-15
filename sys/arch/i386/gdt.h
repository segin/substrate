#ifndef GDT_H
#define GDT_H

#include <stdint.h>

struct gdt_entry_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));
typedef struct gdt_entry_struct gdt_entry_t;

struct gdt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));
typedef struct gdt_ptr_struct gdt_ptr_t;

struct tss_entry_struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;    /* Offset to I/O permission bitmap */
    /* I/O Permission Bitmap: 8192 bytes = 65536 ports (1 bit per port) */
    /* 0 = allow, 1 = deny (trap to GPF) */
    uint8_t iomap[8192];
    uint8_t iomap_end;      /* Must be 0xFF (terminator byte) */
} __attribute__((packed));
typedef struct tss_entry_struct tss_entry_t;

void gdt_init();
void set_kernel_stack(uint32_t stack);

/* TSS I/O Bitmap control (TASKS.md L570) */
void tss_iomap_init(void);              /* Initialize I/O bitmap (deny all) */
void tss_set_iomap(uint16_t port, int allow);  /* Allow/deny single port */
void tss_set_iomap_range(uint16_t start, uint16_t end, int allow); /* Allow/deny range */

#endif
