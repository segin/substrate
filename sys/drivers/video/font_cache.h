/*
 * font_cache.h - Font Glyph Cache and Unicode Mapping
 *
 * Hash-table based glyph cache for fast Unicode codepoint to
 * glyph index lookup. Parses PSF1/PSF2 Unicode tables.
 */
#ifndef _FONT_CACHE_H
#define _FONT_CACHE_H

#include <stdint.h>
#include "psf.h"

/* Number of hash buckets (power of 2 for fast modulo) */
#define GLYPH_CACHE_BUCKETS 256

struct glyph_cache_entry {
    uint32_t codepoint;        /* Unicode codepoint */
    uint32_t glyph_index;     /* Index into font glyph array */
    struct glyph_cache_entry *next;
};

struct glyph_cache {
    struct glyph_cache_entry *buckets[GLYPH_CACHE_BUCKETS];
    uint32_t count;            /* Total entries */
    const struct font_info *font;
};

/* Initialize glyph cache for the given font. Builds Unicode mapping
 * from the font's unicode_table if present. Returns 0 on success. */
int glyph_cache_init(struct glyph_cache *cache, const struct font_info *font);

/* Look up glyph index for a Unicode codepoint. Returns glyph index,
 * or 0 (replacement character) if not found. */
uint32_t glyph_cache_lookup(const struct glyph_cache *cache, uint32_t codepoint);

/* Get glyph bitmap data for a Unicode codepoint via the cache. */
const uint8_t *glyph_cache_get(const struct glyph_cache *cache, uint32_t codepoint);

/* Free all cache entries */
void glyph_cache_destroy(struct glyph_cache *cache);

#endif /* _FONT_CACHE_H */
