/*
 * munmap.c - munmap(2) wrapper
 *
 * Counterpart to mmap(); releases a previously-mapped region.
 * Native dispatch wires SYS_MUNMAP (91) to sys_munmap.
 */

#include <sys/syscall.h>
#include <sys/mman.h>
#include <stddef.h>

long syscall(long number, ...);

int munmap(void *addr, size_t length) {
    return (int)syscall(SYS_MUNMAP, addr, length);
}
