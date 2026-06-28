/*
 * Minimal mock of <drivers/video/vga.h> for the acct_compress host test.
 *
 * acct.c carries a stray `#include <drivers/video/vga.h>` but uses no VGA /
 * framebuffer symbols.  The real header drags in fb.h -> fb_console.h ->
 * <sys/vt.h> (vt_state_t) and <arch/x86-common/multiboot.h>, none of which
 * the test environment provides, so stub it out entirely.
 */
#ifndef _MOCK_VGA_H
#define _MOCK_VGA_H
#endif /* _MOCK_VGA_H */
