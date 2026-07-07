#ifndef _KERN_MAIN_H
#define _KERN_MAIN_H

/* Kernel Entry Point */
void kmain(unsigned long magic, unsigned long addr);

extern int syscall_trace_enabled;

#endif
