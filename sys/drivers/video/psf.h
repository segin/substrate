/*
 * psf.h - PC Screen Font (PSF) Parser
 *
 * Supports PSF1 (256/512 glyphs, fixed 8-pixel width) and
 * PSF2 (variable metrics, Unicode table).
 */
#ifndef _PSF_H
#define _PSF_H

#include <stdint.h>
#include <stddef.h>

/* ==================== PSF1 Format ==================== */

#define PSF1_MAGIC0     0x36
#define PSF1_MAGIC1     0x04
#define PSF1_MODE256    0x01
#define PSF1_MODE512    0x02
#define PSF1_MODEHASTAB 0x04  /* Has Unicode table */
#define PSF1_MAXMODE    0x05
#define PSF1_SEPARATOR  0xFFFF
#define PSF1_STARTSEQ   0xFFFE

struct psf1_header {
    uint8_t magic[2];
    uint8_t mode;
    uint8_t charsize;  /* Bytes per glyph (= height, since width is always 8) */
};

/* ==================== PSF2 Format ==================== */

#define PSF2_MAGIC0     0x72
#define PSF2_MAGIC1     0xb5
#define PSF2_MAGIC2     0x4a
#define PSF2_MAGIC3     0x86
#define PSF2_HAS_UNICODE_TABLE 0x01
#define PSF2_MAXVERSION 0
#define PSF2_SEPARATOR  0xFF
#define PSF2_STARTSEQ   0xFE

struct psf2_header {
    uint8_t  magic[4];
    uint32_t version;
    uint32_t headersize;
    uint32_t flags;
    uint32_t numglyph;
    uint32_t bytesperglyph;
    uint32_t height;
    uint32_t width;
};

/* ==================== Generic Font Interface ==================== */

#define FONT_TYPE_BUILTIN  0
#define FONT_TYPE_PSF1     1
#define FONT_TYPE_PSF2     2

struct font_info {
    int          type;
    uint32_t     width;         /* Glyph width in pixels */
    uint32_t     height;        /* Glyph height in pixels */
    uint32_t     numglyphs;     /* Total glyph count */
    uint32_t     bytesperglyph; /* Bytes per glyph bitmap */
    const uint8_t *glyphs;     /* Glyph bitmap data */
    const uint8_t *unicode_table;  /* Unicode mapping table (NULL if none) */
    size_t       unicode_table_size;
};

/* Parse PSF1 font from memory buffer. Returns 0 on success, -1 on error. */
int psf1_parse(const void *data, size_t size, struct font_info *out);

/* Parse PSF2 font from memory buffer. Returns 0 on success, -1 on error. */
int psf2_parse(const void *data, size_t size, struct font_info *out);

/* Auto-detect and parse PSF1 or PSF2. Returns 0 on success, -1 on error. */
int psf_parse(const void *data, size_t size, struct font_info *out);

/* Initialize font_info for the built-in 8x16 VGA font */
void font_init_builtin(struct font_info *out);

/* Get glyph data for a given glyph index. Returns NULL if out of range. */
const uint8_t *font_get_glyph(const struct font_info *font, uint32_t index);

#endif /* _PSF_H */
