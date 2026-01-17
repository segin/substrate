#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <stddef.h>
#include <sys/types.h>

// Protection bits
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

// Mapping flags
#define MAP_SHARED    0x001
#define MAP_PRIVATE   0x002
#define MAP_FIXED     0x010
#define MAP_ANONYMOUS 0x020
#define MAP_32BIT     0x040

// Special value for mmap errors
#define MAP_FAILED ((void *)-1)

// System calls
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t length, int prot);

#endif
