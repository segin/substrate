/*
 * font_cache.c - Font Glyph Cache and Unicode Mapping
 *
 * Parses PSF1/PSF2 Unicode tables to build a hash-table mapping
 * Unicode codepoints to glyph indices for O(1) lookup.
 *
 * PSF1 Unicode table: sequence of uint16_t values per glyph.
 *   0xFFFF = separator (end of glyph's codepoints)
 *   0xFFFE = start of sequence (combining chars, ignored here)
 *
 * PSF2 Unicode table: UTF-8 encoded codepoints per glyph.
 *   0xFF = separator (end of glyph's codepoints)
 *   0xFE = start of sequence (combining chars, ignored here)
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vm/vm_kmem.h>

#include "psf.h"
#include "font_cache.h"

/* FNV-1a hash for 32-bit codepoint */
static uint32_t hash_codepoint(uint32_t cp)
{
    uint32_t h = 2166136261u;
    h ^= cp & 0xFF;         h *= 16777619u;
    h ^= (cp >> 8) & 0xFF;  h *= 16777619u;
    h ^= (cp >> 16) & 0xFF; h *= 16777619u;
    h ^= (cp >> 24) & 0xFF; h *= 16777619u;
    return h;
}

static int cache_insert(struct glyph_cache *cache, uint32_t codepoint,
                         uint32_t glyph_index)
{
    uint32_t bucket = hash_codepoint(codepoint) & (GLYPH_CACHE_BUCKETS - 1);

    /* Check for duplicate */
    for (struct glyph_cache_entry *e = cache->buckets[bucket]; e; e = e->next) {
        if (e->codepoint == codepoint)
            return 0;  /* Already mapped */
    }

    struct glyph_cache_entry *entry = kmalloc(sizeof(*entry));
    if (!entry)
        return -1;

    entry->codepoint = codepoint;
    entry->glyph_index = glyph_index;
    entry->next = cache->buckets[bucket];
    cache->buckets[bucket] = entry;
    cache->count++;
    return 0;
}

/*
 * Parse PSF1 Unicode table.
 * Format: for each glyph, a sequence of uint16_t codepoints,
 * terminated by 0xFFFF. 0xFFFE starts a combining sequence (skipped).
 */
static void parse_psf1_unicode(struct glyph_cache *cache,
                                const uint8_t *table, size_t table_size)
{
    const uint16_t *p = (const uint16_t *)table;
    const uint16_t *end = (const uint16_t *)(table + (table_size & ~1u));
    uint32_t glyph_index = 0;

    while (p < end && glyph_index < cache->font->numglyphs) {
        uint16_t val = *p++;
        if (val == PSF1_SEPARATOR) {
            glyph_index++;
        } else if (val == PSF1_STARTSEQ) {
            /* Skip combining sequences until separator */
            while (p < end && *p != PSF1_SEPARATOR)
                p++;
        } else {
            cache_insert(cache, (uint32_t)val, glyph_index);
        }
    }
}

/*
 * Decode a single UTF-8 codepoint from a byte stream.
 * Returns the codepoint and advances *pp past the consumed bytes.
 * Returns 0xFFFFFFFF on invalid encoding.
 */
static uint32_t decode_utf8(const uint8_t **pp, const uint8_t *end)
{
    const uint8_t *p = *pp;
    if (p >= end)
        return 0xFFFFFFFF;

    uint8_t c = *p++;
    uint32_t cp;
    int remaining;

    if (c < 0x80) {
        cp = c;
        remaining = 0;
    } else if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F;
        remaining = 1;
    } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F;
        remaining = 2;
    } else if ((c & 0xF8) == 0xF0) {
        cp = c & 0x07;
        remaining = 3;
    } else {
        *pp = p;
        return 0xFFFFFFFF;
    }

    while (remaining-- > 0) {
        if (p >= end || (*p & 0xC0) != 0x80) {
            *pp = p;
            return 0xFFFFFFFF;
        }
        cp = (cp << 6) | (*p++ & 0x3F);
    }

    *pp = p;
    return cp;
}

/*
 * Parse PSF2 Unicode table.
 * Format: for each glyph, UTF-8 encoded codepoints.
 *   0xFF = separator (next glyph)
 *   0xFE = start of combining sequence (skipped)
 */
static void parse_psf2_unicode(struct glyph_cache *cache,
                                const uint8_t *table, size_t table_size)
{
    const uint8_t *p = table;
    const uint8_t *end = table + table_size;
    uint32_t glyph_index = 0;

    while (p < end && glyph_index < cache->font->numglyphs) {
        uint8_t c = *p;

        if (c == PSF2_SEPARATOR) {
            p++;
            glyph_index++;
        } else if (c == PSF2_STARTSEQ) {
            /* Skip combining sequence until separator */
            p++;
            while (p < end && *p != PSF2_SEPARATOR)
                p++;
        } else {
            uint32_t cp = decode_utf8(&p, end);
            if (cp != 0xFFFFFFFF)
                cache_insert(cache, cp, glyph_index);
        }
    }
}

int glyph_cache_init(struct glyph_cache *cache, const struct font_info *font)
{
    memset(cache, 0, sizeof(*cache));
    cache->font = font;

    /* For fonts without a Unicode table, build a 1:1 identity map
     * for the first 128 ASCII codepoints (common case) */
    if (!font->unicode_table || font->unicode_table_size == 0) {
        uint32_t max = font->numglyphs;
        if (max > 256)
            max = 256;
        for (uint32_t i = 0; i < max; i++)
            cache_insert(cache, i, i);
        return 0;
    }

    /* Parse the font-specific Unicode table */
    if (font->type == FONT_TYPE_PSF1) {
        parse_psf1_unicode(cache, font->unicode_table,
                           font->unicode_table_size);
    } else if (font->type == FONT_TYPE_PSF2) {
        parse_psf2_unicode(cache, font->unicode_table,
                           font->unicode_table_size);
    }

    return 0;
}

uint32_t glyph_cache_lookup(const struct glyph_cache *cache, uint32_t codepoint)
{
    uint32_t bucket = hash_codepoint(codepoint) & (GLYPH_CACHE_BUCKETS - 1);

    for (const struct glyph_cache_entry *e = cache->buckets[bucket]; e; e = e->next) {
        if (e->codepoint == codepoint)
            return e->glyph_index;
    }

    /* Fallback: if codepoint is in direct glyph range, use identity */
    if (codepoint < cache->font->numglyphs)
        return codepoint;

    return 0;  /* Replacement glyph (index 0) */
}

const uint8_t *glyph_cache_get(const struct glyph_cache *cache,
                                uint32_t codepoint)
{
    uint32_t index = glyph_cache_lookup(cache, codepoint);
    return font_get_glyph(cache->font, index);
}

void glyph_cache_destroy(struct glyph_cache *cache)
{
    for (int i = 0; i < GLYPH_CACHE_BUCKETS; i++) {
        struct glyph_cache_entry *e = cache->buckets[i];
        while (e) {
            struct glyph_cache_entry *next = e->next;
            kfree(e, sizeof(*e));
            e = next;
        }
        cache->buckets[i] = NULL;
    }
    cache->count = 0;
}
