/*
 * xout286.h - Microsoft x.out, 80286 (16-bit segmented) flavour.
 *
 * Shares the on-disk structures with the 386 loader (see xout.h); what
 * differs is everything about how the image is *executed*:
 *
 *   - every segment is a 16-bit (D/B=0) descriptor with a byte-granular
 *     limit, so no segment may exceed 64 KiB;
 *   - text is split across as many code segments as the middle/large model
 *     needed, each with its own selector baked into the binary's far calls;
 *   - the first data segment is DGROUP: DS == ES == SS, with the process
 *     stack at its top and the break growing up from the end of bss;
 *   - system calls trap through `int $5` with the number in AX and the
 *     arguments in BX/CX/SI/DI (see exec/perso/perso_sco_x286.c).
 *
 * Reference: SCO Xenix/286 2.3 x.out(4), and the Xenix 286 Development
 * System's crt0 (`start`, `__syscall` at text offset 2, `__stkgrow` at 4).
 */
#ifndef _EXEC_FORMATS_XOUT286_H
#define _EXEC_FORMATS_XOUT286_H

#include <stdint.h>

/*
 * Linear placement.  Each x.out segment owns one naturally-aligned 64 KiB
 * window, indexed by its LDT slot, so a selector's descriptor base is a pure
 * function of the selector.  The first Xenix selector is 0x3f (LDT slot 7)
 * and the tables observed in the wild stay well under 64 slots.
 */
#define XOUT286_WINDOW_BASE   0x20000000U   /* 512 MiB */
#define XOUT286_WINDOW_SIZE   0x00010000U   /* 64 KiB: the 16-bit segment cap */
#define XOUT286_MAX_SEGS      64U

/* Byte-granular 16-bit limit for a full segment. */
#define XOUT286_SEG_LIMIT_MAX 0xFFFFU

/* Stack headroom reserved at the top of DGROUP; the break may not grow into
 * it.  Xenix itself grew the two toward each other and trapped on collision;
 * a fixed gap is simpler and matches what `__stkgrow` asks for in practice. */
#define XOUT286_STACK_RESERVE 0x2000U       /* 8 KiB */

/* Linear base of the window an LDT slot's segment lives at. */
static inline uint32_t xout286_window_base(unsigned int ldt_index) {
    return XOUT286_WINDOW_BASE + (uint32_t)ldt_index * XOUT286_WINDOW_SIZE;
}

void xout286_init_handler(void);

#endif /* _EXEC_FORMATS_XOUT286_H */
