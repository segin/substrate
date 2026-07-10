#ifndef _KERN_IOREMAP_HOST_H
#define _KERN_IOREMAP_HOST_H

/*
 * Host-unit-test declarations for kern/ioremap.c.
 *
 * The real kernel build pulls pmap / CPU state in from <arch/i386/pmap.h>
 * and <arch/i386/cpu.h>, which cannot be compiled on the host.  For the
 * host test build (tests/sys/host_test_ioremap.c) these lightweight shims
 * stand in, so ioremap.c never carries manual extern prototypes or macro
 * definitions in the .c file.
 */

#include <stdint.h>

typedef void *pmap_t;

#define VM_PROT_READ  0x01
#define VM_PROT_WRITE 0x02
#define PTE_PWT       0x08
#define PTE_PCD       0x10
#define PTE_PAT       0x80

pmap_t pmap_kernel(void);
int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags);
void pmap_kremove(uintptr_t va);
int i386_cpu_pat_wc_enabled(void);

#endif /* _KERN_IOREMAP_HOST_H */
