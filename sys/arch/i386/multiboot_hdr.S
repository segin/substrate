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
.long 1280      /* width  (0 = loader picks) */
.long 800       /* height (0 = loader picks) */
.long 32        /* depth  (preferred bpp; 0 = loader picks) */
#else
.long 1         /* mode_type: 1 = EGA text mode */
.long 80        /* width  (columns) */
.long 25        /* height (rows) */
.long 0         /* depth  (unused for text) */
#endif

/*
 * Multiboot 2 header — placed in the same .multiboot section so a
 * single kernel image carries both protocols.  GRUB-EFI hands off via
 * mb2 (it can't drive the mb1 long-mode→32-bit switch reliably);
 * BIOS GRUB and qemu -kernel use mb1.  Same kernel either way.
 *
 * mb2 header layout (struct multiboot_header):
 *   u32 magic, u32 arch, u32 length, u32 checksum, [tag stream], end-tag.
 *
 * Every tag is 8-byte aligned and carries its own type/flags/size.
 */
.balign 8
mb2_hdr_start:
    .long MULTIBOOT2_HEADER_MAGIC
    .long MULTIBOOT2_ARCH_I386
    .long mb2_hdr_end - mb2_hdr_start
    .long -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCH_I386 + (mb2_hdr_end - mb2_hdr_start))

    /* Information-request tag: which mb2 info tags we want from the loader. */
    .balign 8
mb2_inforeq_start:
    .short MULTIBOOT2_HEADER_TAG_INFO_REQUEST
    .short 0                /* flags: 0 = required */
    .long mb2_inforeq_end - mb2_inforeq_start
    .long MULTIBOOT2_TAG_TYPE_CMDLINE
    .long MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO
    .long MULTIBOOT2_TAG_TYPE_MMAP
    .long MULTIBOOT2_TAG_TYPE_FRAMEBUFFER
    .long MULTIBOOT2_TAG_TYPE_ACPI_OLD
    .long MULTIBOOT2_TAG_TYPE_ACPI_NEW
mb2_inforeq_end:

#ifdef SUBSTRATE_MB_FB_MODE
    /* Framebuffer tag — ask the loader to set up a linear FB.
     * flags bit 0 = optional: don't refuse to boot if it can't. */
    .balign 8
mb2_fb_start:
    .short MULTIBOOT2_HEADER_TAG_FRAMEBUFFER
    .short 1
    .long mb2_fb_end - mb2_fb_start
    .long 1280              /* width  (0 = loader picks) */
    .long 800               /* height (0 = loader picks) */
    .long 32                /* depth  (preferred bpp) */
mb2_fb_end:
#else
    /* Console flags tag — EGA-text required.  Loaders that can't
     * provide it (notably EFI GRUB) will refuse to boot the text
     * variant, which is the intent — text is for legacy paths. */
    .balign 8
mb2_console_start:
    .short MULTIBOOT2_HEADER_TAG_CONSOLE_FLAGS
    .short 0
    .long mb2_console_end - mb2_console_start
    .long 2                 /* bit 1 = EGA text mode required */
mb2_console_end:
#endif

    /* End tag (mandatory last). */
    .balign 8
    .short MULTIBOOT2_HEADER_TAG_END
    .short 0
    .long 8
mb2_hdr_end:
