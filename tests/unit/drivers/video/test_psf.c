#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <drivers/video/psf.h>
#include <drivers/video/font.h>

bool test_font_init_builtin(void) {
    struct font_info font;
    font_init_builtin(&font);

    if (font.type != FONT_TYPE_BUILTIN) {
        printf("FAIL: font.type expected %d, got %d\n", FONT_TYPE_BUILTIN, font.type);
        return false;
    }

    if (font.width != 8) {
        printf("FAIL: font.width expected 8, got %d\n", font.width);
        return false;
    }

    if (font.height != 16) {
        printf("FAIL: font.height expected 16, got %d\n", font.height);
        return false;
    }

    if (font.numglyphs != 256) {
        printf("FAIL: font.numglyphs expected 256, got %d\n", font.numglyphs);
        return false;
    }

    if (font.bytesperglyph != 16) {
        printf("FAIL: font.bytesperglyph expected 16, got %d\n", font.bytesperglyph);
        return false;
    }

    if (font.glyphs != font_8x16) {
        printf("FAIL: font.glyphs does not match font_8x16\n");
        return false;
    }

    if (font.unicode_table != NULL) {
        printf("FAIL: font.unicode_table expected NULL\n");
        return false;
    }

    return true;
}

bool test_font_get_glyph(void) {
    struct font_info font;
    font_init_builtin(&font);

    // Test valid index
    const uint8_t *glyph0 = font_get_glyph(&font, 0);
    if (glyph0 != font_8x16) {
        printf("FAIL: glyph 0 address mismatch\n");
        return false;
    }

    const uint8_t *glyph1 = font_get_glyph(&font, 1);
    if (glyph1 != font_8x16 + 16) {
        printf("FAIL: glyph 1 address mismatch\n");
        return false;
    }

    // Test out of bounds
    const uint8_t *glyph256 = font_get_glyph(&font, 256);
    if (glyph256 != NULL) {
        printf("FAIL: glyph 256 should be NULL\n");
        return false;
    }

    return true;
}

bool test_psf1_parse_basic(void) {
    struct psf1_header hdr;
    hdr.magic[0] = PSF1_MAGIC0;
    hdr.magic[1] = PSF1_MAGIC1;
    hdr.mode = PSF1_MODE256;
    hdr.charsize = 16;

    uint8_t buffer[sizeof(struct psf1_header) + 256 * 16];
    memcpy(buffer, &hdr, sizeof(hdr));
    memset(buffer + sizeof(hdr), 0xAA, 256 * 16);

    struct font_info font;
    if (psf1_parse(buffer, sizeof(buffer), &font) != 0) {
        printf("FAIL: psf1_parse failed\n");
        return false;
    }

    if (font.type != FONT_TYPE_PSF1 || font.width != 8 || font.height != 16 ||
        font.numglyphs != 256 || font.bytesperglyph != 16) {
        printf("FAIL: psf1_parse metrics mismatch\n");
        return false;
    }

    if (font.glyphs != buffer + sizeof(struct psf1_header)) {
        printf("FAIL: psf1_parse glyphs pointer mismatch\n");
        return false;
    }

    return true;
}

bool test_psf2_parse_basic(void) {
    struct psf2_header hdr;
    hdr.magic[0] = PSF2_MAGIC0;
    hdr.magic[1] = PSF2_MAGIC1;
    hdr.magic[2] = PSF2_MAGIC2;
    hdr.magic[3] = PSF2_MAGIC3;
    hdr.version = 0;
    hdr.headersize = sizeof(struct psf2_header);
    hdr.flags = 0;
    hdr.numglyph = 512;
    hdr.height = 20;
    hdr.width = 10;
    hdr.bytesperglyph = 20 * 2; // ceil(10/8) = 2

    uint8_t buffer[sizeof(struct psf2_header) + 512 * 40];
    memcpy(buffer, &hdr, sizeof(hdr));

    struct font_info font;
    if (psf2_parse(buffer, sizeof(buffer), &font) != 0) {
        printf("FAIL: psf2_parse failed\n");
        return false;
    }

    if (font.type != FONT_TYPE_PSF2 || font.width != 10 || font.height != 20 ||
        font.numglyphs != 512 || font.bytesperglyph != 40) {
        printf("FAIL: psf2_parse metrics mismatch\n");
        return false;
    }

    return true;
}

bool test_psf_parse_dispatch(void) {
    struct psf1_header hdr1;
    hdr1.magic[0] = PSF1_MAGIC0;
    hdr1.magic[1] = PSF1_MAGIC1;
    hdr1.mode = PSF1_MODE256;
    hdr1.charsize = 16;

    struct font_info font;
    if (psf_parse(&hdr1, sizeof(hdr1) + 256*16, &font) != 0) {
        printf("FAIL: psf_parse dispatch to PSF1 failed\n");
        return false;
    }
    if (font.type != FONT_TYPE_PSF1) {
         printf("FAIL: psf_parse expected PSF1\n");
         return false;
    }

    struct psf2_header hdr2;
    memset(&hdr2, 0, sizeof(hdr2));
    hdr2.magic[0] = PSF2_MAGIC0;
    hdr2.magic[1] = PSF2_MAGIC1;
    hdr2.magic[2] = PSF2_MAGIC2;
    hdr2.magic[3] = PSF2_MAGIC3;
    hdr2.headersize = sizeof(hdr2);
    hdr2.numglyph = 1;
    hdr2.width = 8;
    hdr2.height = 16;
    hdr2.bytesperglyph = 16;

    if (psf_parse(&hdr2, sizeof(hdr2) + 16, &font) != 0) {
        printf("FAIL: psf_parse dispatch to PSF2 failed\n");
        return false;
    }
    if (font.type != FONT_TYPE_PSF2) {
         printf("FAIL: psf_parse expected PSF2\n");
         return false;
    }

    return true;
}
