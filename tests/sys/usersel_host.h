/*
 * usersel_host.h - LAR's answer for substrate's i386 GDT, for host tests.
 *
 * <machine/usersel.h> asks the CPU (LAR) whether a sigreturn frame's
 * selectors may be loaded.  On the host that asks the host's own tables,
 * where 0x1B and 0x23 are not substrate's user segments, so every frame
 * looked bad and sigreturn took the SIGSEGV path.  Under HOST_TEST the
 * header calls usersel_lar() instead; include this in exactly one file of
 * each host test that builds a sigreturn path.
 *
 * The table is sys/arch/i386/gdt.c's: 3 user code (0xFB), 4 user data
 * (0xF3), 6 TLS (0xF3).  The kernel's own entries (DPL 0) and the TSS are
 * not accessible at RPL 3, and host tests have no LDT.
 */
#ifndef USERSEL_HOST_H
#define USERSEL_HOST_H

#include <stdint.h>

int usersel_lar(uint16_t sel, uint32_t *ar)
{
    if (sel & 4)                        /* LDT */
        return 0;
    switch (sel >> 3) {
    case 3:
        *ar = 0xFB00;
        return 1;
    case 4:
    case 6:
        *ar = 0xF300;
        return 1;
    default:
        return 0;
    }
}

#endif /* USERSEL_HOST_H */
