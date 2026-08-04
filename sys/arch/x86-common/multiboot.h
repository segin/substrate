#ifndef _MULTIBOOT_H
#define _MULTIBOOT_H

#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS 0x00000007
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Multiboot 2 — kept side-by-side with multiboot 1 in the kernel image
 * so EFI loaders (which can't drive the mb1 long-mode→32-bit switch
 * reliably) can hand off via mb2 instead.  Both headers describe the
 * same kernel; the loader picks whichever it implements. */
#define MULTIBOOT2_HEADER_MAGIC          0xE85250D6
#define MULTIBOOT2_BOOTLOADER_MAGIC      0x36D76289
#define MULTIBOOT2_ARCH_I386             0

#define MULTIBOOT2_HEADER_TAG_END                  0
#define MULTIBOOT2_HEADER_TAG_INFO_REQUEST         1
#define MULTIBOOT2_HEADER_TAG_CONSOLE_FLAGS        4
#define MULTIBOOT2_HEADER_TAG_FRAMEBUFFER          5

#define MULTIBOOT2_TAG_TYPE_END                    0
#define MULTIBOOT2_TAG_TYPE_CMDLINE                1
#define MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME       2
#define MULTIBOOT2_TAG_TYPE_MODULE                 3
#define MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO          4
#define MULTIBOOT2_TAG_TYPE_BOOTDEV                5
#define MULTIBOOT2_TAG_TYPE_MMAP                   6
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER            8
#define MULTIBOOT2_TAG_TYPE_ELF_SECTIONS           9
#define MULTIBOOT2_TAG_TYPE_ACPI_OLD              14
#define MULTIBOOT2_TAG_TYPE_ACPI_NEW              15
#define MULTIBOOT2_TAG_TYPE_EFI_MMAP              17
#define MULTIBOOT2_TAG_TYPE_EFI64_IMAGE_HANDLE    20

/*
 * RSDP handed over by the bootloader (multiboot2 tags 14/15).
 *
 * Under UEFI the ACPI RSDP is reachable only through the EFI configuration
 * table -- it is NOT in the legacy 0xE0000-0xFFFFF window that BIOS systems
 * put it in, so the classic signature scan finds nothing.  GRUB already
 * copies the RSDP into the multiboot2 info for us; this hands that copy to
 * the ACPI code.  Returns NULL when the bootloader supplied none (a BIOS
 * boot, where the legacy scan works).
 *
 * Guarded: this header is included from boot.S, where a C declaration is a
 * syntax error to the assembler.
 */
#ifndef __ASSEMBLER__
const void *multiboot_get_acpi_rsdp(void);
#endif

#define MULTIBOOT_INFO_MEMORY           0x00000001
#define MULTIBOOT_INFO_BOOTDEV          0x00000002
#define MULTIBOOT_INFO_CMDLINE          0x00000004
#define MULTIBOOT_INFO_MODS             0x00000008
#define MULTIBOOT_INFO_AOUT_SYMS        0x00000010
#define MULTIBOOT_INFO_ELF_SHDR         0x00000020
#define MULTIBOOT_INFO_MEM_MAP          0x00000040
#define MULTIBOOT_INFO_DRIVE_INFO       0x00000080
#define MULTIBOOT_INFO_CONFIG_TABLE     0x00000100
#define MULTIBOOT_INFO_BOOT_LOADER_NAME 0x00000200
#define MULTIBOOT_INFO_APM_TABLE        0x00000400
#define MULTIBOOT_INFO_VBE_INFO         0x00000800
#define MULTIBOOT_INFO_FRAMEBUFFER_INFO 0x00001000

#ifndef ASM_FILE
#include <stdint.h>

typedef struct multiboot_info {
	uint32_t flags;
	uint32_t mem_lower;
	uint32_t mem_upper;
	uint32_t boot_device;
	uint32_t cmdline;
	uint32_t mods_count;
	uint32_t mods_addr;
	uint32_t syms[4];
	uint32_t mmap_length;
	uint32_t mmap_addr;
	uint32_t drives_length;
	uint32_t drives_addr;
	uint32_t config_table;
	uint32_t boot_loader_name;
	uint32_t apm_table;
	uint32_t vbe_control_info;
	uint32_t vbe_mode_info;
	uint16_t vbe_mode;
	uint16_t vbe_interface_seg;
	uint16_t vbe_interface_off;
	uint16_t vbe_interface_len;

	uint64_t framebuffer_addr;
	uint32_t framebuffer_pitch;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint8_t  framebuffer_bpp;
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB     1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2
	uint8_t  framebuffer_type;
	union {
		struct {
			uint32_t framebuffer_palette_addr;
			uint16_t framebuffer_palette_num_colors;
		};
		struct {
			uint8_t framebuffer_red_field_position;
			uint8_t framebuffer_red_mask_size;
			uint8_t framebuffer_green_field_position;
			uint8_t framebuffer_green_mask_size;
			uint8_t framebuffer_blue_field_position;
			uint8_t framebuffer_blue_mask_size;
		};
	};
} multiboot_info_t;

typedef struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
#define MULTIBOOT_MEMORY_AVAILABLE              1
#define MULTIBOOT_MEMORY_RESERVED               2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE       3
#define MULTIBOOT_MEMORY_NVS                    4
#define MULTIBOOT_MEMORY_BADRAM                 5
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

/* Multiboot 2 boot-info structures (passed by loader to kernel). */
typedef struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
} multiboot2_tag_t;

typedef struct multiboot2_tag_string {
    uint32_t type;
    uint32_t size;
    char     string[0];
} multiboot2_tag_string_t;

typedef struct multiboot2_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} multiboot2_tag_basic_meminfo_t;

typedef struct multiboot2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed)) multiboot2_mmap_entry_t;

typedef struct multiboot2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    multiboot2_mmap_entry_t entries[0];
} multiboot2_tag_mmap_t;

typedef struct multiboot2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;
    uint16_t reserved;
    uint8_t  red_field_pos;
    uint8_t  red_mask_size;
    uint8_t  green_field_pos;
    uint8_t  green_mask_size;
    uint8_t  blue_field_pos;
    uint8_t  blue_mask_size;
} __attribute__((packed)) multiboot2_tag_framebuffer_t;

#endif

#endif
