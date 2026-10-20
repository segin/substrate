/*
 * A BDF font that does not end in a NUL.
 *
 * bdf_parse's scanners (starts_with's strncmp, parse_int, the field walks)
 * stop at a NUL, not at the end of the buffer, so a font file whose last
 * line has no terminator was read past its end.  The font here is placed
 * so it ends exactly at a page boundary with an unmapped page after it:
 * reading one byte too far is a SIGSEGV.  It must parse, and get its
 * glyph.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *p, size_t size) { (void)size; free(p); }

#include "../../sys/drivers/video/bdf_pcf.c"

/* No trailing newline: "ENDFONT" is the last byte. */
static const char font[] =
    "STARTFONT 2.1\n"
    "FONT test\n"
    "SIZE 8 75 75\n"
    "FONTBOUNDINGBOX 8 2 0 0\n"
    "CHARS 1\n"
    "STARTCHAR A\n"
    "ENCODING 65\n"
    "SWIDTH 500 0\n"
    "DWIDTH 8 0\n"
    "BBX 8 2 0 0\n"
    "BITMAP\n"
    "81\n"
    "7E\n"
    "ENDCHAR\n"
    "ENDFONT";

int main(void)
{
    long pg = sysconf(_SC_PAGESIZE);
    size_t len = sizeof(font) - 1;          /* no NUL */
    char *area = mmap(NULL, (size_t)pg * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area == MAP_FAILED) {
        printf("FAIL: mmap\n");
        return 1;
    }
    munmap(area + pg, (size_t)pg);          /* the guard */
    char *text = area + pg - len;
    memcpy(text, font, len);

    struct font_info fi;
    memset(&fi, 0, sizeof(fi));
    if (bdf_parse(text, len, &fi) != 0) {
        printf("FAIL: bdf_parse refused the font\n");
        return 1;
    }
    if (fi.width != 8 || fi.height != 2 || !fi.glyphs) {
        printf("FAIL: font is %ux%u\n", (unsigned)fi.width, (unsigned)fi.height);
        return 1;
    }
    bdf_free(&fi);
    printf("PASS: an unterminated BDF font ending at a page edge parses\n");
    return 0;
}
