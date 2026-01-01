#include "panic.h"
#include "../drivers/video/vga.h"

void panic(const char *msg) {
    vga_write("\nKERNEL PANIC: ", 15);
    vga_write(msg, 0); // Need strlen, assuming vga_write handles 0 as 'calc len' or I assume user provides len? 
    // vga_write implementation takes len. 
    // I'll implement a simple strlen here or include string.h
    const char *p = msg;
    int len = 0;
    while (*p++) len++;
    vga_write(msg, len);
    
    // Halt
    while(1) __asm__ volatile("cli; hlt");
}
