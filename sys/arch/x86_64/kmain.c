/*
 * kmain.c - x86_64 kernel entry
 *
 * Reached from boot.S in 64-bit mode at the higher half, with the boot page
 * tables live (layout.h).  Brings up the serial console, drops the identity
 * window boot.S needed for the jump to the higher half, and hands over to
 * the machine-independent kmain(), with the loader's information reached
 * through the direct map, exactly as the i386 boot code hands it a
 * higher-half pointer.
 */
#include <stdint.h>

#include <arch/x86_64/boot.h>
#include <arch/x86_64/cons.h>
#include <arch/x86_64/layout.h>
#include <kern/main.h>

void kmain64(uint32_t magic, uint32_t info_phys) {
    econs_init();
    econs_puts("\nSubstrate x86_64 kernel\n");

    /* Nothing below the kernel half is mapped from here on; the first
     * process's address space is the first user of PML4 slot 0. */
    boot_pml4[0] = 0;
    __asm__ volatile("movq %%cr3, %%rax; movq %%rax, %%cr3"
                     ::: "rax", "memory");

    kmain(magic, (unsigned long)(uintptr_t)phys_to_dmap(info_phys));

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
