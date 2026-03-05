#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// Fake vm86 struct and call
struct vm86_regs {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t es;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
};

// Simulate context switch cost (spin loop roughly representing VM86 switch overhead)
void vm86_bios_call(int int_no, struct vm86_regs *regs) {
    (void)int_no;
    (void)regs;
    for (volatile int i = 0; i < 10000; i++); // Expensive operation
}

static void bios_putc(char c) {
    if (c == '\n') bios_putc('\r');
    struct vm86_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0E00 | (unsigned char)c;
    regs.ebx = 0x0007; // Page 0, Light Grey
    vm86_bios_call(0x10, &regs);
}

static void bios_puts_unoptimized(const char *s) {
    while (*s) bios_putc(*s++);
}

#define BIOS_STRING_BUFFER 0x3000

// Optimized version
static void bios_puts_optimized(const char *s) {
    char buf[512]; // Simulated memory buffer
    size_t len = 0;

    // Convert \n to \r\n and copy
    while (*s && len < 512) {
        if (*s == '\n') {
            buf[len++] = '\r';
        }
        if (len < 512) {
            buf[len++] = *s++;
        }
    }

    if (len == 0) return;

    struct vm86_regs regs;

    // Get cursor position (AH=0x03, BH=0)
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0300;
    regs.ebx = 0x0000;
    vm86_bios_call(0x10, &regs);

    uint16_t cursor_pos = regs.edx & 0xFFFF; // DH = row, DL = col

    // Write string (AH=0x13, AL=0x01: update cursor)
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x1301;
    regs.ebx = 0x0007; // Page 0, Light Grey
    regs.ecx = len;
    regs.edx = cursor_pos;
    regs.es  = BIOS_STRING_BUFFER >> 4;
    regs.ebp = BIOS_STRING_BUFFER & 0xF;
    vm86_bios_call(0x10, &regs);
}

int main() {
    const char* text = "Substrate Kernel Video Selection\n================================\n\n1. Standard Text Mode (80x25)\n2. VESA 1024x768x32\n3. VESA 800x600x32\n4. VESA 640x480x32\n\nSelect mode [1-4]: ";

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i=0; i<100; i++) {
        bios_puts_unoptimized(text);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double t_unopt = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i=0; i<100; i++) {
        bios_puts_optimized(text);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double t_opt = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Unoptimized: %f seconds\n", t_unopt);
    printf("Optimized:   %f seconds\n", t_opt);
    printf("Improvement: %f x\n", t_unopt / t_opt);

    return 0;
}
