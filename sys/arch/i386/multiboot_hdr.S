#define ASM_FILE 1
#include "multiboot.h"

/*
 * Multiboot 1 header.  Two variants are built from this single source:
 *
 *   default                    legacy EGA text mode (80x25)
 *   -DSUBSTRATE_MB_FB_MODE     linear framebuffer, GRUB picks resolution
 *
 * EFI GRUB cannot satisfy mode_type=1 because UEFI firmware does not
 * expose an EGA-text-mode equivalent — it is framebuffer (GOP) only.
 * The framebuffer variant is what gets loaded from a UEFI ESP.
 *
 * The header must sit in the first 8 KiB of the kernel image; the
 * linker script (arch/i386/linker.ld) places .multiboot at the very
 * top of the .setup output section to guarantee that.
 *
 * `_start` is defined in boot.S — the `jmp _start` at the head of this
 * section is the entry-fallback path for non-ELF loaders (Linux zImage).
 * Multiboot-aware loaders read the ELF entry point instead.
 */

.section .multiboot
.align 4
    jmp _start
    .align 4
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_HEADER_FLAGS
.long -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)
/* Address fields (offsets 12-28): unused since bit 16 is not set, but
 * must be present so the video mode fields land at offsets 32-44. */
.long 0         /* header_addr   (ignored) */
.long 0         /* load_addr     (ignored) */
.long 0         /* load_end_addr (ignored) */
.long 0         /* bss_end_addr  (ignored) */
.long 0         /* entry_addr    (ignored) */
/* Video mode fields (offsets 32-44): used since bit 2 (VIDEO) is set. */
#ifdef SUBSTRATE_MB_FB_MODE
.long 0         /* mode_type: 0 = linear framebuffer */
.long 0         /* width  (0 = loader picks) */
.long 0         /* height (0 = loader picks) */
.long 32        /* depth  (preferred bpp; 0 = loader picks) */
#else
.long 1         /* mode_type: 1 = EGA text mode */
.long 80        /* width  (columns) */
.long 25        /* height (rows) */
.long 0         /* depth  (unused for text) */
#endif
