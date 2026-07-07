#ifndef _KERN_MAIN_H
#define _KERN_MAIN_H

/* Kernel Entry Point */
void kmain(unsigned long magic, unsigned long addr);

extern int syscall_trace_enabled;

/* Dump per-personality syscall counters (reset=1 clears after print) */
void syscall_stats_dump(int reset);

#endif
