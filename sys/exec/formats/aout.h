/*
 * a.out loader for Substrate.  Handles Linux, FreeBSD/NetBSD/OpenBSD, and
 * SunOS-flavour 32-bit a.out images.  See docs/specs/aout_loader_spec.md
 * for the classification model — the short version is:
 *
 *   - The header occupies 32 bytes with a single 32-bit "midmag" field
 *     packing magic (low 16) and machine-id (high 10).
 *   - Magic discriminates the layout (OMAGIC/NMAGIC/ZMAGIC/QMAGIC).
 *   - QMAGIC is Linux-exclusive.
 *   - Non-zero MID + BSD-style relocation tables imply NetBSD/OpenBSD.
 *   - Old FreeBSD ignores MID and tolerates host-endian; relocs decide.
 *
 * The classifier is a *hint*; the personality (and its syscall table)
 * still has to be selected by exec.  The loader's job is structural
 * correctness — it never picks a personality on its own.
 */

#ifndef _AOUT_H
#define _AOUT_H

#include <stdint.h>
#include <stddef.h>

#define AOUT_OMAGIC_VAL  0407   /* OMAGIC: text+data writable, no separate I/D. */
#define AOUT_NMAGIC_VAL  0410   /* NMAGIC: separate I/D, text RO, data RW. */
#define AOUT_ZMAGIC_VAL  0413   /* ZMAGIC: demand-paged, page-aligned text. */
#define AOUT_QMAGIC_VAL  0314   /* QMAGIC: Linux compact ZMAGIC, 1st page unmapped. */

/*
 * Header layout shared by all 32-bit a.out variants.  Total 32 bytes.
 * On little-endian hosts (i386/Sun386i/Linux x86) midmag is consumed
 * directly; for big-endian variants we'd byteswap before classifying.
 */
struct aout_exec {
    uint32_t a_midmag;   /* magic in low 16, MID in [16:25], flags in [26:31] */
    uint32_t a_text;     /* text segment size in bytes */
    uint32_t a_data;     /* initialised data size */
    uint32_t a_bss;      /* zero-init BSS size */
    uint32_t a_syms;     /* symbol table size */
    uint32_t a_entry;    /* entry point virtual address */
    uint32_t a_trsize;   /* text relocation table size */
    uint32_t a_drsize;   /* data relocation table size */
};

#define AOUT_HEADER_SIZE  32U

/*
 * File offset of the text segment (Linux's N_TXTOFF).  ZMAGIC leaves a
 * 1 KiB gap ahead of the text, QMAGIC maps the header as the head of the
 * text segment, OMAGIC/NMAGIC put text immediately after the header.
 *
 * Whether this offset is page-aligned decides how the image can be loaded,
 * and it is the same test Linux's binfmt_aout makes: only QMAGIC (offset 0)
 * can be demand-paged from the file segment by segment; every other magic
 * must be read flat into one anonymous region.
 */
#define AOUT_ZMAGIC_TXTOFF  1024U

#define AOUT_TXTOFF(magic)                                                  \
    ((magic) == AOUT_ZMAGIC_VAL ? AOUT_ZMAGIC_TXTOFF :                      \
     ((magic) == AOUT_QMAGIC_VAL ? 0U : AOUT_HEADER_SIZE))

/* Helper macros for parsing midmag. */
#define AOUT_GETMAGIC(mm)  ((uint32_t)(mm) & 0xFFFFU)
#define AOUT_GETMID(mm)    (((uint32_t)(mm) >> 16) & 0x3FFU)
#define AOUT_GETFLAGS(mm)  (((uint32_t)(mm) >> 26) & 0x3FU)

/* Common machine IDs.  The kernel only ever runs i386 a.out images, but
 * MID is a reliable BSD/Linux discriminator: BSD writes a real MID and
 * Linux usually writes 0. */
#define AOUT_MID_NONE    0     /* unset; typical for Linux a.out */
#define AOUT_MID_M68K    1
#define AOUT_MID_SPARC   3
#define AOUT_MID_I386    100   /* NetBSD i386 MID */
#define AOUT_MID_I386_BSD 134  /* old FreeBSD i386 (legacy) */
#define AOUT_MID_SUN386  151   /* SunOS 4.0.x Sun386i */

enum aout_flavor {
    AOUT_FLAVOR_UNKNOWN = 0,
    AOUT_FLAVOR_LINUX,
    AOUT_FLAVOR_FREEBSD,
    AOUT_FLAVOR_NETBSD,
    AOUT_FLAVOR_OPENBSD,
    AOUT_FLAVOR_SUNOS,
};

/*
 * aout_classify - determine the most likely flavour for a header.
 *
 * Hard rules from docs/specs/aout_loader_spec.md:
 *   - QMAGIC -> Linux (no other flavour issues QMAGIC).
 *   - MID == AOUT_MID_SUN386 -> SunOS 4.0.x (Sun386i only).
 *   - MID != 0 + non-empty relocation tables -> NetBSD (caller can
 *     refine to OpenBSD via additional metadata).
 *   - MID == 0 + non-empty relocation tables -> old FreeBSD.
 *   - Otherwise -> Linux (fallback for the most common case).
 *
 * Caller passes an aout_exec read directly off the file in the host's
 * native endian.  Big-endian binaries (m68k/sparc) are not supported
 * by the i386 kernel and will classify as UNKNOWN.
 */
enum aout_flavor aout_classify(const struct aout_exec *hdr);

/*
 * aout_validate_header - structural validation independent of flavour.
 *
 *   - magic is one of OMAGIC/NMAGIC/ZMAGIC/QMAGIC
 *   - segment sizes are bounded (< 1 GiB each) and non-overflowing when
 *     summed
 *   - entry is non-zero for executable images
 *   - file_size is large enough to hold what the header claims
 *
 * Returns 0 if usable, -1 otherwise.  Pure-C, no allocations.
 */
int aout_validate_header(const struct aout_exec *hdr, uint32_t file_size);

/* Register the kernel-side a.out (Linux ZMAGIC/QMAGIC/OMAGIC) exec handler.
 *
 * Declared unconditionally: exec_init() calls it unconditionally, so hiding
 * the prototype under HOST_TEST did not remove the call, it only left the
 * host build with an implicit declaration -- an error under C23.  A host test
 * that links exec.c supplies its own definition. */
void aout_init_handler(void);

#ifndef HOST_TEST
/* Linux uselib(2): map an old-style a.out shared library (libc.so.4, ld.so)
 * at its fixed embedded load address into the caller's address space. */
int aout_sys_uselib(uint32_t upath, uint32_t a1, uint32_t a2, uint32_t a3,
                    uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7);
#endif

#endif /* _AOUT_H */
