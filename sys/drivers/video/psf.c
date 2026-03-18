/*
 * psf.c - PC Screen Font (PSF) Parser
 *
 * Parses PSF1 and PSF2 font formats from memory buffers.
 * PSF1: 2-byte magic, mode, charsize; always 8 pixels wide.
 * PSF2: 32-byte header; variable width/height, optional Unicode table.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "psf.h"
#include "font.h"

int psf1_parse(const void *data, size_t size, struct font_info *out)
{
    const struct psf1_header *hdr = (const struct psf1_header *)data;

    if (size < sizeof(struct psf1_header))
        return -1;

    if (hdr->magic[0] != PSF1_MAGIC0 || hdr->magic[1] != PSF1_MAGIC1)
        return -1;

    uint32_t numglyphs = (hdr->mode & PSF1_MODE512) ? 512 : 256;
    uint32_t bytesperglyph = hdr->charsize;

    /* Validate data size */
    size_t glyph_data_size = (size_t)numglyphs * bytesperglyph;
    if (size < sizeof(struct psf1_header) + glyph_data_size)
        return -1;

    memset(out, 0, sizeof(*out));
    out->type = FONT_TYPE_PSF1;
    out->width = 8;  /* PSF1 is always 8 pixels wide */
    out->height = bytesperglyph;
    out->numglyphs = numglyphs;
    out->bytesperglyph = bytesperglyph;
    out->glyphs = (const uint8_t *)data + sizeof(struct psf1_header);

    /* Unicode table follows glyph data if present */
    if (hdr->mode & PSF1_MODEHASTAB) {
        size_t table_offset = sizeof(struct psf1_header) + glyph_data_size;
        if (table_offset < size) {
            out->unicode_table = (const uint8_t *)data + table_offset;
            out->unicode_table_size = size - table_offset;
        }
    }

    return 0;
}

int psf2_parse(const void *data, size_t size, struct font_info *out)
{
    const struct psf2_header *hdr = (const struct psf2_header *)data;

    if (size < sizeof(struct psf2_header))
        return -1;

    if (hdr->magic[0] != PSF2_MAGIC0 || hdr->magic[1] != PSF2_MAGIC1 ||
        hdr->magic[2] != PSF2_MAGIC2 || hdr->magic[3] != PSF2_MAGIC3)
        return -1;

    if (hdr->version > PSF2_MAXVERSION)
        return -1;

    if (hdr->headersize < sizeof(struct psf2_header))
        return -1;

    /* Validate glyph data fits */
    size_t glyph_data_size = (size_t)hdr->numglyph * hdr->bytesperglyph;
    if (size < hdr->headersize + glyph_data_size)
        return -1;

    /* Sanity check dimensions */
    if (hdr->width == 0 || hdr->height == 0 || hdr->numglyph == 0)
        return -1;

    /* Verify bytesperglyph matches dimensions (rows * ceil(width/8)) */
    uint32_t expected_bpg = hdr->height * ((hdr->width + 7) / 8);
    if (hdr->bytesperglyph < expected_bpg)
        return -1;

    memset(out, 0, sizeof(*out));
    out->type = FONT_TYPE_PSF2;
    out->width = hdr->width;
    out->height = hdr->height;
    out->numglyphs = hdr->numglyph;
    out->bytesperglyph = hdr->bytesperglyph;
    out->glyphs = (const uint8_t *)data + hdr->headersize;

    /* Unicode table follows glyph data if present */
    if (hdr->flags & PSF2_HAS_UNICODE_TABLE) {
        size_t table_offset = hdr->headersize + glyph_data_size;
        if (table_offset < size) {
            out->unicode_table = (const uint8_t *)data + table_offset;
            out->unicode_table_size = size - table_offset;
        }
    }

    return 0;
}

int psf_parse(const void *data, size_t size, struct font_info *out)
{
    if (size < 4)
        return -1;

    const uint8_t *bytes = (const uint8_t *)data;

    /* Try PSF2 first (more specific 4-byte magic) */
    if (bytes[0] == PSF2_MAGIC0 && bytes[1] == PSF2_MAGIC1 &&
        bytes[2] == PSF2_MAGIC2 && bytes[3] == PSF2_MAGIC3)
        return psf2_parse(data, size, out);

    /* Try PSF1 */
    if (bytes[0] == PSF1_MAGIC0 && bytes[1] == PSF1_MAGIC1)
        return psf1_parse(data, size, out);

    return -1;
}

void font_init_builtin(struct font_info *out)
{
    memset(out, 0, sizeof(*out));
    out->type = FONT_TYPE_BUILTIN;
    out->width = 8;
    out->height = 16;
    out->numglyphs = 256;
    out->bytesperglyph = 16;
    out->glyphs = font_8x16;
    out->unicode_table = NULL;
    out->unicode_table_size = 0;
}

const uint8_t *font_get_glyph(const struct font_info *font, uint32_t index)
{
    if (index >= font->numglyphs)
        return NULL;
    return font->glyphs + index * font->bytesperglyph;
}
