/*
 * bios_menu.c - Real-mode Video Selection Menu using VM86 BIOS calls
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <drivers/console/console.h>
#include <sys/vm86.h>
#include <drivers/video/fb.h>

/* Fix implicit declaration of vm86_bios_call if header failed */
int vm86_bios_call(int int_no, struct vm86_regs *regs);

/* Low memory buffer addresses (must be < 1MB and not conflict with stack/code) */
#define VBE_INFO_BLOCK_ADDR  0xA000
#define VBE_MODE_INFO_ADDR   0xA200

/* VESA Structures (Partial) */
struct vbe_mode_info_block {
    uint16_t mode_attributes;
    uint8_t  win_a_attributes;
    uint8_t  win_b_attributes;
    uint16_t win_granularity;
    uint16_t win_size;
    uint16_t win_a_segment;
    uint16_t win_b_segment;
    uint32_t win_func_ptr;
    uint16_t bytes_per_scanline;
    uint16_t x_resolution;
    uint16_t y_resolution;
    uint8_t  x_char_size;
    uint8_t  y_char_size;
    uint8_t  number_of_planes;
    uint8_t  bits_per_pixel;
    uint8_t  number_of_banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  number_of_image_pages;
    uint8_t  reserved1;
    uint8_t  red_mask_size;
    uint8_t  red_field_position;
    uint8_t  green_mask_size;
    uint8_t  green_field_position;
    uint8_t  blue_mask_size;
    uint8_t  blue_field_position;
    uint8_t  rsvd_mask_size;
    uint8_t  rsvd_field_position;
    uint8_t  direct_color_mode_info;
    uint32_t phys_base_ptr;
    uint32_t reserved2;
    uint16_t reserved3;
} __attribute__((packed));

static void bios_putc(char c) {
    if (c == '\n') bios_putc('\r');
    struct vm86_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0E00 | (unsigned char)c;
    regs.ebx = 0x0007; // Page 0, Light Grey
    vm86_bios_call(0x10, &regs);
}

static void bios_puts(const char *s) {
    while (*s) bios_putc(*s++);
}

static int bios_getc(void) {
    struct vm86_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0000;
    vm86_bios_call(0x16, &regs);
    return regs.eax & 0xFF; // AL = ASCII char
}

/* Returns: 0 for Text, 1 for VESA (and updates fb info) */
int video_ask_mode(fb_info_t *fb) {
    /* 1. Reset to standard 80x25 text mode first for menu interaction */
    struct vm86_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0003;
    vm86_bios_call(0x10, &regs);
    
    /* 2. Loop Menu */
    while (1) {
        bios_puts("\nSubstrate Kernel Video Selection\n");
        bios_puts("================================\n\n");
        bios_puts("1. Standard Text Mode (80x25)\n");
        bios_puts("2. VESA 1024x768x32\n");
        bios_puts("3. VESA 800x600x32\n");
        bios_puts("4. VESA 640x480x32\n");
        bios_puts("\nSelect mode [1-4]: ");
        
        int c = bios_getc();
        bios_putc(c);
        bios_putc('\n');
        
        if (c == '1') {
            regs.eax = 0x0003;
            vm86_bios_call(0x10, &regs);
            return 0; // Text mode
        }
        
        uint16_t vesa_mode = 0;
        if (c == '2') vesa_mode = 0x118; // 1024x768 24/32
        else if (c == '3') vesa_mode = 0x115; // 800x600 24/32
        else if (c == '4') vesa_mode = 0x112; // 640x480 24/32
        
        if (vesa_mode) {
            /* Try to set VESA mode */
            /* Add bit 14 for LFB */
            vesa_mode |= 0x4000;
            
            memset(&regs, 0, sizeof(regs));
            regs.eax = 0x4F02;
            regs.ebx = vesa_mode;
            vm86_bios_call(0x10, &regs);
            
            if ((regs.eax & 0xFFFF) != 0x004F) {
                bios_puts("Error setting VESA mode!\n");
                continue;
            }
            
            /* Get Mode Info to populate fb struct */
            memset(&regs, 0, sizeof(regs));
            regs.eax = 0x4F01;
            regs.ecx = vesa_mode & 0x3FFF; // Clear LFB bit for query? Usually safe.
            regs.es  = VBE_MODE_INFO_ADDR >> 4;
            regs.edi = VBE_MODE_INFO_ADDR & 0xF;
            vm86_bios_call(0x10, &regs);
            
            if ((regs.eax & 0xFFFF) != 0x004F) {
                bios_puts("Error querying VESA mode info!\n");
                continue;
            }
            
            /* Read back struct from low memory */
            struct vbe_mode_info_block *info = (struct vbe_mode_info_block*)VBE_MODE_INFO_ADDR;
            
            fb->addr = (uint32_t*)(uintptr_t)info->phys_base_ptr;
            fb->width = info->x_resolution;
            fb->height = info->y_resolution;
            fb->pitch = info->bytes_per_scanline;
            fb->bpp = info->bits_per_pixel;
            fb->putpixel = NULL; // Use fallback
            
            return 1; // VESA mode
        }
    }
}
