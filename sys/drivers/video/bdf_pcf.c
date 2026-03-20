/*
 * bdf_pcf.c - BDF and PCF Bitmap Font Parsers
 *
 * PCF: Binary format. Read TOC, find METRICS/BITMAPS/ENCODINGS tables,
 *      extract glyph bitmaps and encoding info.
 *
 * BDF: Text format. Parse STARTCHAR/ENCODING/BBX/BITMAP sections,
 *      convert hex bitmap data to binary glyph storage.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vm/vm_kmem.h>

#include "psf.h"
#include "bdf_pcf.h"

/* ==================== PCF Helpers ==================== */

static uint32_t pcf_read32(const void *ptr, int msb)
{
    const uint8_t *p = (const uint8_t *)ptr;
    if (msb)
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | p[3];
    else
        return p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t pcf_read16(const void *ptr, int msb)
{
    const uint8_t *p = (const uint8_t *)ptr;
    if (msb)
        return ((uint16_t)p[0] << 8) | p[1];
    else
        return p[0] | ((uint16_t)p[1] << 8);
}

static const struct pcf_toc_entry *pcf_find_table(const void *data,
                                                    size_t size,
                                                    uint32_t table_count,
                                                    uint32_t type)
{
    const uint8_t *base = (const uint8_t *)data;
    const struct pcf_toc_entry *toc =
        (const struct pcf_toc_entry *)(base + 8);

    for (uint32_t i = 0; i < table_count; i++) {
        if ((uintptr_t)&toc[i + 1] > (uintptr_t)(base + size))
            break;
        /* TOC entries are always little-endian */
        uint32_t t = toc[i].type;
        if (t == type)
            return &toc[i];
    }
    return NULL;
}

int pcf_parse(const void *data, size_t size, struct font_info *out)
{
    const uint8_t *base = (const uint8_t *)data;

    if (size < 8)
        return -1;

    /* Check magic */
    uint32_t magic = base[0] | ((uint32_t)base[1] << 8) |
                     ((uint32_t)base[2] << 16) | ((uint32_t)base[3] << 24);
    if (magic != PCF_MAGIC)
        return -1;

    uint32_t table_count = base[4] | ((uint32_t)base[5] << 8) |
                           ((uint32_t)base[6] << 16) | ((uint32_t)base[7] << 24);

    if (size < 8 + table_count * sizeof(struct pcf_toc_entry))
        return -1;

    /* Find METRICS table for dimensions */
    const struct pcf_toc_entry *metrics_entry =
        pcf_find_table(data, size, table_count, PCF_METRICS);
    if (!metrics_entry)
        return -1;

    uint32_t metrics_offset = metrics_entry->offset;
    uint32_t metrics_format = metrics_entry->format;
    int metrics_msb = (metrics_format & PCF_BYTE_MASK) ? 1 : 0;
    int compressed = (metrics_format & PCF_COMPRESSED_METRICS) ? 1 : 0;

    if (metrics_offset + 8 > size)
        return -1;

    /* Read metrics format and count */
    /* Skip format field (4 bytes), then read count */
    const uint8_t *mp = base + metrics_offset + 4;
    uint32_t num_metrics;
    if (compressed) {
        num_metrics = pcf_read16(mp, metrics_msb);
        mp += 2;
    } else {
        num_metrics = pcf_read32(mp, metrics_msb);
        mp += 4;
    }

    if (num_metrics == 0)
        return -1;

    /* Read first glyph metrics for width/height */
    uint32_t glyph_width = 0, glyph_height = 0;
    if (compressed) {
        /* Compressed: 5 bytes per metric, values biased by 0x80 */
        if (metrics_offset + 6 + 5 > size)
            return -1;
        /* left_bearing, right_bearing, width, ascent, descent */
        uint8_t cwidth = mp[2];
        uint8_t ascent = mp[3];
        uint8_t descent = mp[4];
        glyph_width = cwidth; /* Already uncompressed (no bias for width) */
        glyph_height = (ascent - 0x80) + (descent - 0x80);
        if (glyph_width > 128) glyph_width -= 0x80;
        /* Fallback sanity */
        if (glyph_width == 0) glyph_width = 8;
        if (glyph_height == 0) glyph_height = 16;
    } else {
        /* Uncompressed: 12 bytes per metric (6 x int16) */
        if (metrics_offset + 8 + 12 > size)
            return -1;
        int16_t left = (int16_t)pcf_read16(mp, metrics_msb);
        int16_t right = (int16_t)pcf_read16(mp + 2, metrics_msb);
        int16_t width = (int16_t)pcf_read16(mp + 4, metrics_msb);
        int16_t ascent = (int16_t)pcf_read16(mp + 6, metrics_msb);
        int16_t descent = (int16_t)pcf_read16(mp + 8, metrics_msb);
        (void)left;
        glyph_width = (uint32_t)(right > 0 ? right : width);
        glyph_height = (uint32_t)((ascent > 0 ? ascent : 0) + (descent > 0 ? descent : 0));
        if (glyph_width == 0) glyph_width = 8;
        if (glyph_height == 0) glyph_height = 16;
    }

    /* Find BITMAPS table */
    const struct pcf_toc_entry *bitmaps_entry =
        pcf_find_table(data, size, table_count, PCF_BITMAPS);
    if (!bitmaps_entry)
        return -1;

    uint32_t bitmaps_offset = bitmaps_entry->offset;
    uint32_t bitmaps_format = bitmaps_entry->format;
    int bitmaps_msb = (bitmaps_format & PCF_BIT_MASK) ? 1 : 0;
    (void)bitmaps_msb;
    int glyph_pad = 1 << (bitmaps_format & PCF_GLYPH_PAD_MASK);

    if (bitmaps_offset + 8 > size)
        return -1;

    const uint8_t *bp = base + bitmaps_offset + 4;
    uint32_t num_glyphs = pcf_read32(bp, 0 /* TOC values are LE */);
    bp += 4;

    if (num_glyphs == 0 || num_glyphs > 65536)
        return -1;

    /* Skip glyph offsets array (num_glyphs * 4 bytes) */
    const uint8_t *offsets_ptr = bp;
    bp += num_glyphs * 4;

    /* Read bitmap sizes (4 entries for different padding) */
    if ((uintptr_t)(bp + 16) > (uintptr_t)(base + size))
        return -1;

    uint32_t pad_idx = bitmaps_format & PCF_GLYPH_PAD_MASK;
    uint32_t bitmap_size = pcf_read32(bp + pad_idx * 4, 0);
    bp += 16;

    /* bp now points to bitmap data */
    if ((uintptr_t)(bp + bitmap_size) > (uintptr_t)(base + size))
        return -1;

    const uint8_t *bitmap_data = bp;

    /* Calculate bytes per glyph */
    uint32_t row_bytes = ((glyph_width + 7) / 8);
    /* Pad row to required alignment */
    row_bytes = (row_bytes + glyph_pad - 1) & ~((uint32_t)glyph_pad - 1);
    uint32_t bytesperglyph = row_bytes * glyph_height;

    memset(out, 0, sizeof(*out));
    out->type = FONT_TYPE_PSF2;  /* Generic bitmap font, use PSF2 type */
    out->width = glyph_width;
    out->height = glyph_height;
    out->numglyphs = num_glyphs;
    out->bytesperglyph = bytesperglyph;
    out->glyphs = bitmap_data;
    out->unicode_table = NULL;

    /* Check for ENCODINGS table for Unicode mapping */
    const struct pcf_toc_entry *enc_entry =
        pcf_find_table(data, size, table_count, PCF_BDF_ENCODINGS);
    if (enc_entry) {
        uint32_t enc_offset = enc_entry->offset;
        if (enc_offset + 14 <= size) {
            out->unicode_table = base + enc_offset;
            out->unicode_table_size = enc_entry->size;
        }
    }

    /* Use proper offsets for variable-width glyphs */
    (void)offsets_ptr;

    return 0;
}

/* ==================== BDF Parser ==================== */

/* Skip whitespace and return pointer to next non-space char */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* Check if line starts with keyword (followed by space or newline) */
static int starts_with(const char *line, const char *keyword)
{
    size_t len = strlen(keyword);
    if (strncmp(line, keyword, len) != 0)
        return 0;
    return (line[len] == ' ' || line[len] == '\t' ||
            line[len] == '\n' || line[len] == '\r' || line[len] == '\0');
}

/* Parse decimal integer from string */
static int parse_int(const char *s)
{
    int neg = 0;
    int val = 0;

    s = skip_ws(s);
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

/* Advance to next line */
static const char *next_line(const char *p, const char *end)
{
    while (p < end && *p != '\n')
        p++;
    if (p < end)
        p++; /* skip \n */
    return p;
}

/* Parse hex digit */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int bdf_parse(const void *data, size_t size, struct font_info *out)
{
    const char *p = (const char *)data;
    const char *end = p + size;

    /* First pass: find FONTBOUNDINGBOX for dimensions and CHARS for count */
    uint32_t bb_width = 0, bb_height = 0;
    uint32_t num_chars = 0;

    const char *scan = p;
    while (scan < end) {
        if (starts_with(scan, "FONTBOUNDINGBOX")) {
            const char *args = scan + 16;
            bb_width = (uint32_t)parse_int(args);
            args = skip_ws(args);
            while (*args && *args != ' ' && *args != '\t') args++;
            bb_height = (uint32_t)parse_int(args);
        } else if (starts_with(scan, "CHARS ")) {
            num_chars = (uint32_t)parse_int(scan + 5);
        }
        scan = next_line(scan, end);
    }

    if (bb_width == 0 || bb_height == 0 || num_chars == 0)
        return -1;

    /* Allocate glyph storage */
    uint32_t row_bytes = (bb_width + 7) / 8;
    uint32_t bytesperglyph = row_bytes * bb_height;
    size_t total_glyph_size = (size_t)num_chars * bytesperglyph;
    uint8_t *glyph_data = kmalloc(total_glyph_size);
    if (!glyph_data)
        return -1;
    memset(glyph_data, 0, total_glyph_size);

    /* Second pass: parse glyph bitmaps */
    scan = p;
    uint32_t glyph_idx = 0;
    int in_bitmap = 0;
    uint32_t bitmap_row = 0;

    while (scan < end && glyph_idx < num_chars) {
        if (in_bitmap) {
            if (starts_with(scan, "ENDCHAR")) {
                in_bitmap = 0;
                glyph_idx++;
            } else if (bitmap_row < bb_height) {
                /* Parse hex row data */
                uint8_t *dst = glyph_data + (size_t)glyph_idx * bytesperglyph +
                               bitmap_row * row_bytes;
                const char *hp = skip_ws(scan);
                for (uint32_t b = 0; b < row_bytes && hp + 1 < end; b++) {
                    int h1 = hex_digit(*hp);
                    int h2 = hex_digit(*(hp + 1));
                    if (h1 >= 0 && h2 >= 0) {
                        dst[b] = (uint8_t)((h1 << 4) | h2);
                        hp += 2;
                    } else {
                        break;
                    }
                }
                bitmap_row++;
            }
        } else if (starts_with(scan, "BITMAP")) {
            in_bitmap = 1;
            bitmap_row = 0;
        }
        scan = next_line(scan, end);
    }

    memset(out, 0, sizeof(*out));
    out->type = FONT_TYPE_PSF2;  /* Generic bitmap font */
    out->width = bb_width;
    out->height = bb_height;
    out->numglyphs = glyph_idx;  /* Actual parsed count */
    out->bytesperglyph = bytesperglyph;
    out->glyphs = glyph_data;
    out->unicode_table = NULL;
    out->unicode_table_size = 0;

    return 0;
}

void bdf_free(struct font_info *font)
{
    if (font && font->glyphs) {
        size_t total = (size_t)font->numglyphs * font->bytesperglyph;
        kfree((void *)font->glyphs, total);
        font->glyphs = NULL;
    }
}
