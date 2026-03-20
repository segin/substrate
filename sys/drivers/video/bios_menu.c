/*
 * bios_menu.c - Real-mode Video Selection Menu using VM86 BIOS calls
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <drivers/console/console.h>
#include <sys/vm86.h>
#include <drivers/video/fb.h>

/* Fix implicit declaration of vm86_bios_call if header failed */
int vm86_bios_call(int int_no, struct vm86_regs *regs);

/* Low memory buffer addresses (must be < 1MB and not conflict with stack/code) */
#define BIOS_STRING_BUFFER   0x3000
#define VBE_INFO_BLOCK_ADDR  0xA000
#define VBE_MODE_INFO_ADDR   0xA200
#define VBE_EDID_BLOCK_ADDR  0xA400

#define VBE_MODE_LIST_MAX    256
#define VBE_SUPPORTED_MAX    64

/* VBE status */
#define VBE_SUCCESS(ax)      (((ax) & 0xFFFFU) == 0x004FU)

struct vbe_info_block {
    char     signature[4];
    uint16_t version;
    uint32_t oem_string_ptr;
    uint32_t capabilities;
    uint32_t video_mode_ptr;
    uint16_t total_memory;
    uint8_t  reserved[236];
    uint8_t  oem_data[256];
} __attribute__((packed));

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

struct vbe_detect_result {
    int ok;
    uint16_t version;
    uint16_t mode_count;
    uint16_t modes[VBE_MODE_LIST_MAX];
};

struct vbe_pm_interface {
    uint16_t seg;
    uint16_t off;
    uint16_t len;
};

static void *vbe_far_ptr_to_linear(uint16_t seg, uint16_t off) {
    return (void *)(uintptr_t)(((uint32_t)seg << 4) + off);
}

static int vbe_detect_and_list(struct vbe_detect_result *out) {
    struct vm86_regs regs;
    struct vbe_info_block *info;
    uint16_t *mode_list;
    uint16_t i;

    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    info = (struct vbe_info_block *)(uintptr_t)VBE_INFO_BLOCK_ADDR;
    memset(info, 0, sizeof(*info));
    info->signature[0] = 'V';
    info->signature[1] = 'B';
    info->signature[2] = 'E';
    info->signature[3] = '2';

    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x4F00;
    regs.es = VBE_INFO_BLOCK_ADDR >> 4;
    regs.edi = VBE_INFO_BLOCK_ADDR & 0xF;
    vm86_bios_call(0x10, &regs);
    if (!VBE_SUCCESS(regs.eax)) {
        return -1;
    }
    if (info->signature[0] != 'V' || info->signature[1] != 'E' ||
        info->signature[2] != 'S' || info->signature[3] != 'A') {
        return -1;
    }

    mode_list = (uint16_t *)vbe_far_ptr_to_linear((uint16_t)(info->video_mode_ptr >> 16),
                                                   (uint16_t)(info->video_mode_ptr & 0xFFFFU));
    if (!mode_list) {
        return -1;
    }

    out->ok = 1;
    out->version = info->version;
    for (i = 0; i < VBE_MODE_LIST_MAX; i++) {
        uint16_t mode = mode_list[i];
        if (mode == 0xFFFFU) {
            break;
        }
        out->modes[i] = mode;
        out->mode_count++;
    }

    return 0;
}

static int vbe_mode_supported(const struct vbe_detect_result *vbe, uint16_t mode) {
    uint16_t i;

    if (!vbe || !vbe->ok) {
        return 0;
    }
    for (i = 0; i < vbe->mode_count; i++) {
        if (vbe->modes[i] == mode) {
            return 1;
        }
    }
    return 0;
}

static int vbe_query_pm_interface(struct vbe_pm_interface *out) {
    struct vm86_regs regs;

    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x4F0A;
    regs.ebx = 0x0000;
    vm86_bios_call(0x10, &regs);
    if (!VBE_SUCCESS(regs.eax)) {
        return -1;
    }

    out->seg = regs.es;
    out->off = regs.edi & 0xFFFFU;
    out->len = regs.ecx & 0xFFFFU;
    return 0;
}

static int vbe_try_read_edid(void) {
    struct vm86_regs regs;
    uint8_t *edid = (uint8_t *)(uintptr_t)VBE_EDID_BLOCK_ADDR;

    memset(edid, 0, 128);

    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x4F15;
    regs.ebx = 0x0000;
    regs.ecx = 0;
    vm86_bios_call(0x10, &regs);
    if (!VBE_SUCCESS(regs.eax)) {
        return -1;
    }

    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x4F15;
    regs.ebx = 0x0001;
    regs.ecx = 0;
    regs.es = VBE_EDID_BLOCK_ADDR >> 4;
    regs.edi = VBE_EDID_BLOCK_ADDR & 0xF;
    vm86_bios_call(0x10, &regs);
    if (!VBE_SUCCESS(regs.eax)) {
        return -1;
    }

    if (edid[0] != 0x00 || edid[1] != 0xFF || edid[2] != 0xFF || edid[3] != 0xFF ||
        edid[4] != 0xFF || edid[5] != 0xFF || edid[6] != 0xFF || edid[7] != 0x00) {
        return -1;
    }

    return 0;
}

static void bios_putc(char c) {
    if (c == '\n') bios_putc('\r');
    struct vm86_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x0E00 | (unsigned char)c;
    regs.ebx = 0x0007; // Page 0, Light Grey
    vm86_bios_call(0x10, &regs);
}

/* Buffer at 0x3000 used for batched string output via BIOS INT 10h, AH=13h */
#define BIOS_STRING_BUF_MAX 4000

static void bios_puts(const char *s) {
    char *buf = (char *)(uintptr_t)BIOS_STRING_BUFFER;
    struct vm86_regs regs;

    while (*s) {
        int len = 0;

        while (*s && len < BIOS_STRING_BUF_MAX - 1) {
            if (*s == '\n') {
                buf[len++] = '\r';
                if (len >= BIOS_STRING_BUF_MAX - 1) {
                    break;
                }
            }
            buf[len++] = *s++;
        }

        if (len == 0) {
            return;
        }

        memset(&regs, 0, sizeof(regs));
        regs.eax = 0x0300;
        regs.ebx = 0x0000;
        vm86_bios_call(0x10, &regs);
        uint16_t cursor_pos = regs.edx & 0xFFFF;

        memset(&regs, 0, sizeof(regs));
        regs.eax = 0x1301;
        regs.ebx = 0x0007;
        regs.ecx = len;
        regs.edx = cursor_pos;
        regs.es  = BIOS_STRING_BUFFER >> 4;
        regs.ebp = BIOS_STRING_BUFFER & 0xF;
        vm86_bios_call(0x10, &regs);
    }
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
    
    struct vbe_detect_result vbe;
    struct vbe_pm_interface pm32;

    if (vbe_detect_and_list(&vbe) == 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "VBE %u.%u detected (%u modes).\n",
                 (unsigned int)(vbe.version >> 8),
                 (unsigned int)(vbe.version & 0xFF),
                 (unsigned int)vbe.mode_count);
        bios_puts(msg);

        if (vbe_query_pm_interface(&pm32) == 0 && pm32.len != 0) {
            bios_puts("VBE PM32 interface available.\n");
        } else {
            bios_puts("VBE PM32 interface unavailable.\n");
        }

        if (vbe_try_read_edid() == 0) {
            bios_puts("EDID retrieval succeeded.\n");
        } else {
            bios_puts("EDID retrieval unavailable.\n");
        }
    } else {
        memset(&vbe, 0, sizeof(vbe));
        bios_puts("VBE not detected; text mode only.\n");
    }

    /* 2. Loop Menu */
    while (1) {
        bios_puts("\nSubstrate Kernel Video Selection\n");
        bios_puts("================================\n\n");
        bios_puts("1. Standard Text Mode (80x25)\n");
        if (vbe_mode_supported(&vbe, 0x118)) bios_puts("2. VESA 1024x768x32\n");
        if (vbe_mode_supported(&vbe, 0x115)) bios_puts("3. VESA 800x600x32\n");
        if (vbe_mode_supported(&vbe, 0x112)) bios_puts("4. VESA 640x480x32\n");
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
        if (c == '2' && vbe_mode_supported(&vbe, 0x118)) vesa_mode = 0x118; // 1024x768 24/32
        else if (c == '3' && vbe_mode_supported(&vbe, 0x115)) vesa_mode = 0x115; // 800x600 24/32
        else if (c == '4' && vbe_mode_supported(&vbe, 0x112)) vesa_mode = 0x112; // 640x480 24/32
        
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
