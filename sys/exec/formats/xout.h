/*
 * xout.h - Microsoft x.out segmented executable format (Xenix/386)
 *
 * The x.out format is the native executable format of SCO Xenix and early
 * System V.3 on the i386.  It describes a 386 *segmented* program: text and
 * data live in distinct LDT segments selected by fixed selectors baked into
 * the binary, and system calls are made through the SysV/386 `lcall $7,$0`
 * call gate rather than a software interrupt.
 *
 * Layout on disk:
 *   struct xexec   (32 bytes) at offset 0    -- the a.out-style header
 *   struct xext    (xe_ext bytes) immediately after xexec   -- extension
 *   struct xseg[]  at xe_segpos              -- the segment table
 *   segment file images at each xs_filpos
 *
 * References: SCO x.out.h, iBCS2, "The Xenix 386 C library".
 */
#ifndef _EXEC_FORMATS_XOUT_H
#define _EXEC_FORMATS_XOUT_H

#include <stdint.h>

#define XOUT_MAGIC      0x0206U  /* x_magic: Microsoft x.out */

/*
 * x_cpu: the target processor lives in the low nibble.  This is what tells a
 * 16-bit segmented Xenix/286 image (loaded by xout286.c, run under the
 * SCO-X/286 personality) apart from a 32-bit Xenix/386 one (xout.c) -- both
 * carry x_magic 0x0206, so the magic alone cannot dispatch.
 */
#define XC_CPU_MASK     0x0FU
#define XC_8086         0x04U    /* Intel 8086/8088 */
#define XC_80286        0x09U    /* Intel 80286, 16-bit protected mode */
#define XC_80386        0x0AU    /* Intel 80386, 32-bit */
#define XC_80186        0x0BU    /* Intel 80186 */
#define XC_MIDDLE       0x20U    /* middle/large model (far code) */

/* x_renv run-time environment flags */
#define XE_EXEC         0x0001U  /* executable (fully linked) */
#define XE_SEP          0x0002U  /* separate I & D address spaces */
#define XE_PURE         0x0004U  /* pure (shareable) text */
#define XE_FS           0x0008U  /* fixed stack (xe_stksize valid) */
#define XE_OVER         0x0010U  /* overlay */
#define XE_LDATA        0x0020U  /* large data model */
#define XE_LTEXT        0x0040U  /* large text model (many code segments) */
#define XE_ABS          0x0400U  /* absolute memory image (standalone) */
#define XE_SEG          0x0800U  /* segmented (has a segment table) */
#define XE_VMOD         0xC000U  /* version field mask */
#define XE_V2           0x4000U  /* Xenix 2.3 */
#define XE_V3           0x8000U  /* Xenix 3.0 */
#define XE_V5           0xC000U  /* Xenix System V */

/* xs_type segment types */
#define XS_TEXT         1        /* text (code) segment */
#define XS_DATA         2        /* data segment */
#define XS_SYMS         3        /* symbol table segment */
#define XS_REL          4        /* relocation segment */

/* xs_attr segment attribute bits (subset) */
#define XS_ATTR_HUGE    0x0400U
#define XS_ATTR_BIGDATA 0x0200U

/* The x.out primary header (a.out compatible prefix). 32 bytes. */
struct xexec {
    uint16_t x_magic;    /* 0x0206 */
    uint16_t x_ext;      /* size of the xext extension that follows */
    int32_t  x_text;     /* size of text */
    int32_t  x_data;     /* size of initialized data */
    int32_t  x_bss;      /* size of uninitialized data */
    int32_t  x_syms;     /* size of symbol table */
    int32_t  x_reloc;    /* size of relocation table */
    int32_t  x_entry;    /* entry point (offset within the entry segment) */
    uint8_t  x_cpu;      /* cpu type & byte order (0x4a = i386) */
    uint8_t  x_relsym;   /* relocation & symbol format */
    uint16_t x_renv;     /* run-time environment (XE_* flags) */
};

/* The x.out header extension.  Positional 44-byte layout for Xenix/386. */
struct xext {
    int32_t  xe_trsize;   /* text relocation size */
    int32_t  xe_drsize;   /* data relocation size */
    int32_t  xe_tbase;    /* text relocation base */
    int32_t  xe_dbase;    /* data relocation base */
    int32_t  xe_stksize;  /* stack size (valid if XE_FS) */
    int32_t  xe_segpos;   /* file offset of the segment table */
    int32_t  xe_segsize;  /* byte size of the segment table */
    int32_t  xe_mdtpos;   /* machine-dependent table file offset */
    int32_t  xe_mdtsize;  /* machine-dependent table size */
    uint8_t  xe_mdttype;  /* machine-dependent table type */
    uint8_t  xe_pagesize; /* file page size in multiples of 512 bytes */
    uint8_t  xe_ostype;   /* operating system type */
    uint8_t  xe_osvers;   /* operating system version */
    uint16_t xe_eseg;     /* entry segment selector */
    uint16_t xe_sres;     /* reserved */
};

/* One entry in the x.out segment table.  20 meaningful bytes; the stride
 * between entries is xe_segsize / (number of segments) -- 32 in practice. */
struct xseg {
    uint16_t xs_type;    /* XS_TEXT / XS_DATA / ... */
    uint16_t xs_attr;    /* attribute bits */
    uint16_t xs_seg;     /* segment selector (RPL/TI baked in) */
    uint16_t xs_align;   /* alignment (log2) */
    int32_t  xs_filpos;  /* file offset of the segment image */
    int32_t  xs_psize;   /* physical (on-disk) size */
    int32_t  xs_vsize;   /* initialized virtual size (data + explicit bss) */
    int32_t  xs_msize;   /* total in-memory reservation (bss/heap; >= xs_vsize) */
};

#define XOUT_SEG_STRIDE 32U      /* on-disk stride between xseg entries */

/* Selector helpers: an x.out selector has RPL(1:0)=3, TI(2)=1 (LDT). */
#define XOUT_SEL_INDEX(sel)  ((unsigned int)((sel) >> 3))
#define XOUT_LDT_SELECTOR(i) ((uint16_t)(((i) << 3) | 0x04U | 0x03U))

void xout_init_handler(void);

#endif /* _EXEC_FORMATS_XOUT_H */
