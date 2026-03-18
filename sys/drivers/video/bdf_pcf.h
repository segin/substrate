/*
 * bdf_pcf.h - BDF and PCF Bitmap Font Support
 *
 * BDF (Bitmap Distribution Format): Adobe text-based font format.
 * PCF (Portable Compiled Format): X11 compiled binary font format.
 *
 * Both are parsed into the generic font_info structure from psf.h.
 */
#ifndef _BDF_PCF_H
#define _BDF_PCF_H

#include <stdint.h>
#include <stddef.h>
#include "psf.h"

/* ==================== PCF Format Structures ==================== */

#define PCF_MAGIC         0x70636601  /* "pcf\1" */

/* PCF table types */
#define PCF_PROPERTIES       (1 << 0)
#define PCF_ACCELERATORS     (1 << 1)
#define PCF_METRICS          (1 << 2)
#define PCF_BITMAPS          (1 << 3)
#define PCF_INK_METRICS      (1 << 4)
#define PCF_BDF_ENCODINGS    (1 << 5)
#define PCF_SWIDTHS          (1 << 6)
#define PCF_GLYPH_NAMES      (1 << 7)
#define PCF_BDF_ACCELERATORS (1 << 8)

/* PCF format flags */
#define PCF_DEFAULT_FORMAT   0x00000000
#define PCF_INKBOUNDS        0x00000200
#define PCF_ACCEL_W_INKBOUNDS 0x00000100
#define PCF_COMPRESSED_METRICS 0x00000100

/* Byte order */
#define PCF_BYTE_MASK        (1 << 2)  /* 0=LSBFirst, 1=MSBFirst */
#define PCF_BIT_MASK         (1 << 3)  /* 0=LSBFirst, 1=MSBFirst */
#define PCF_GLYPH_PAD_MASK   (3 << 0)  /* Glyph padding: 0=bytes, 1=shorts, 2=ints */
#define PCF_SCAN_UNIT_MASK   (3 << 4)  /* Scan unit: 0=bytes, 1=shorts, 2=ints */

struct pcf_toc_entry {
    uint32_t type;
    uint32_t format;
    uint32_t size;
    uint32_t offset;
};

/* ==================== API ==================== */

/*
 * Parse a PCF (Portable Compiled Format) font from memory.
 * Extracts metrics and bitmap data into font_info.
 * Returns 0 on success, -1 on error.
 *
 * Note: The caller must keep the data buffer valid for the lifetime
 * of the font_info, as glyphs points into the buffer.
 */
int pcf_parse(const void *data, size_t size, struct font_info *out);

/*
 * Parse a BDF (Bitmap Distribution Format) font from memory.
 * BDF is a text format, so this requires allocating glyph storage.
 * Returns 0 on success, -1 on error.
 *
 * Unlike PCF/PSF, BDF parsing allocates memory for the glyph data
 * since it must be converted from hex text to binary. Call
 * bdf_free() when done.
 */
int bdf_parse(const void *data, size_t size, struct font_info *out);

/* Free allocated glyph data from bdf_parse() */
void bdf_free(struct font_info *font);

#endif /* _BDF_PCF_H */
