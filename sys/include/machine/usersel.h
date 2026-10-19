/*
 * machine/usersel.h - is a segment selector safe to hand back to user mode?
 *
 * sigreturn restores segment registers from a frame the program wrote.
 * Forcing RPL 3 is not enough: a selector past the end of the GDT or LDT,
 * or naming a system descriptor or a kernel segment, makes the return to
 * user mode fault in the kernel while it reloads DS/ES/FS/GS or IRETs to
 * CS:SS.  LAR answers exactly the question the CPU will ask: it succeeds
 * only for a selector inside the table limit whose descriptor is
 * accessible at the selector's RPL (3 here), and yields its access rights,
 * from which the type is checked.  Valid in protected and long mode.
 */
#ifndef _MACHINE_USERSEL_H
#define _MACHINE_USERSEL_H

#include <stdint.h>

#define USERSEL_CS    0   /* code segment for CS */
#define USERSEL_SS    1   /* writable data segment for SS */
#define USERSEL_DATA  2   /* DS/ES/FS/GS: null, data, or readable code */

/* Access-rights bits as LAR returns them (descriptor bits 8-15). */
#define USERSEL_AR_P      0x8000   /* present */
#define USERSEL_AR_DPL    0x6000   /* privilege level */
#define USERSEL_AR_S      0x1000   /* code/data, not system */
#define USERSEL_AR_CODE   0x0800   /* code (else data) */
#define USERSEL_AR_RW     0x0200   /* code: readable; data: writable */

#ifdef HOST_TEST
/* A host test runs on the host's descriptor tables, where substrate's
 * selectors mean something else; the test supplies LAR's answer for
 * substrate's GDT (tests/sys/usersel_host.h). */
int usersel_lar(uint16_t sel, uint32_t *ar);
#else
/* LAR: nonzero, with the access rights in *ar, if `sel` (RPL included)
 * names a descriptor accessible at that privilege. */
static inline int usersel_lar(uint16_t sel, uint32_t *ar)
{
    uint8_t ok;
    __asm__ volatile ("lar %k2, %0\n\t"
                      "setz %1"
                      : "=r"(*ar), "=q"(ok)
                      : "r"((uint32_t)sel)
                      : "cc");
    return ok;
}
#endif

/* Nonzero if `sel` may be loaded into a user segment register of `kind`.
 * The caller should still force RPL 3; this checks the descriptor. */
static inline int user_selector_ok(uint16_t sel, int kind)
{
    sel |= 3;
    if ((sel & ~3u) == 0)              /* null selector */
        return kind == USERSEL_DATA;

    uint32_t ar = 0;
    if (!usersel_lar(sel, &ar))
        return 0;
    if ((ar & (USERSEL_AR_P | USERSEL_AR_S)) != (USERSEL_AR_P | USERSEL_AR_S))
        return 0;
    if ((ar & USERSEL_AR_DPL) != USERSEL_AR_DPL)
        return 0;
    switch (kind) {
    case USERSEL_CS:
        return (ar & USERSEL_AR_CODE) != 0;
    case USERSEL_SS:
        return !(ar & USERSEL_AR_CODE) && (ar & USERSEL_AR_RW);
    default:
        return !(ar & USERSEL_AR_CODE) || (ar & USERSEL_AR_RW);
    }
}

/* Nonzero if a restored 32-bit user frame's selectors are all loadable:
 * what every sigreturn checks before returning to the frame. */
static inline int user_frame_selectors_ok(uint32_t cs, uint32_t ss,
                                          uint32_t ds, uint32_t es,
                                          uint32_t fs, uint32_t gs)
{
    return user_selector_ok((uint16_t)cs, USERSEL_CS) &&
           user_selector_ok((uint16_t)ss, USERSEL_SS) &&
           user_selector_ok((uint16_t)ds, USERSEL_DATA) &&
           user_selector_ok((uint16_t)es, USERSEL_DATA) &&
           user_selector_ok((uint16_t)fs, USERSEL_DATA) &&
           user_selector_ok((uint16_t)gs, USERSEL_DATA);
}

#endif /* _MACHINE_USERSEL_H */
