#include <sys/types.h>
#include <sys/file.h>
#include <kern/console.h>
#include <kern/cmdline.h>
#include <string.h>
#include <drivers/video/fb.h>
#include <arch/x86-common/io.h>
#include "../../kern/resource.h"
#include "../../kern/pci.h"

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

#define VBE_DISPI_INDEX_ID          0
#define VBE_DISPI_INDEX_XRES        1
#define VBE_DISPI_INDEX_YRES        2
#define VBE_DISPI_INDEX_BPP         3
#define VBE_DISPI_INDEX_ENABLE      4
#define VBE_DISPI_INDEX_BANK        5
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET    8
#define VBE_DISPI_INDEX_Y_OFFSET    9

#define VBE_DISPI_ID0               0xB0C0
#define VBE_DISPI_ID1               0xB0C1
#define VBE_DISPI_ID2               0xB0C2
#define VBE_DISPI_ID3               0xB0C3
#define VBE_DISPI_ID4               0xB0C4
#define VBE_DISPI_ID5               0xB0C5

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_LFB_ENABLED       0x40
#define BGA_BANK_WINDOW_PHYS        0x000A0000U
#define BGA_BANK_WINDOW_VIRT        0xC00A0000U
#define BGA_BANK_SIZE               0x10000U

void bga_scroll(int y_offset);
static int bga_set_viewport(int x, int y);
static void bga_banked_putpixel(int x, int y, uint32_t color);

static void bga_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t bga_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

int bga_is_available(void) {
    uint16_t id = bga_read(VBE_DISPI_INDEX_ID);
    return (id >= VBE_DISPI_ID0 && id <= VBE_DISPI_ID5);
}

/* video_driver_t.probe contract: 0 = success.  bga_is_available() uses
 * the opposite convention (truthy = present), so we wrap it. */
static int bga_probe_driver(void) {
    return bga_is_available() ? 0 : -1;
}

// Fixed LFB address for BGA is usually at BAR0 of the PCI device, 
// but traditionally for Bochs/QEMU it is at 0xE0000000 (ISA hole/high mem).
// Ideally we should use PCI enumeration to find the device (VENDOR=0x1234, DEVICE=0x1111).
// For simplicity, we'll assume 0xE0000000 if PCI scanning isn't hooked up yet for this.
// Wait, we have pci_init. We should try to find it via PCI for correctness?
// Let's stick to simple init for now, maybe scan PCI later or assume E0000000.
// QEMU: -vga std puts it at 0xFD000000 often. 
// Using PCI is safer.

static uint32_t bga_lfb_addr = 0;
static void *bga_lfb_virt = NULL;
static size_t bga_lfb_mapped_size = 0;
static int bga_use_lfb = 1;
static uint16_t bga_current_bank = 0xFFFFU;

/* Resolve the Bochs Graphics Adapter's LFB physical address by scanning
 * PCI config space directly.  We cannot use pci_find_device() because
 * bga_init() runs BEFORE pci_init() during boot — the device list is
 * empty at this point.  Do a targeted bus-0 walk for vendor=0x1234
 * device=0x1111 (Bochs/QEMU stdvga) and read BAR0.  The hardcoded
 * 0xE0000000 fallback only worked on specific QEMU machine types;
 * modern qemu places the Bochs BAR at 0xFD000000 or higher, and writes
 * to the wrong physical region silently leak into RAM (the pitch-black
 * framebuffer we kept seeing). */
static uint32_t bga_discover_lfb_phys(void) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id = pci_read_config32(0, slot, 0, 0x00);
        if ((id & 0xFFFFU) != 0x1234U) continue;
        if (((id >> 16) & 0xFFFFU) != 0x1111U) continue;
        uint32_t bar0 = pci_read_config32(0, slot, 0, 0x10);
        /* Low 4 bits encode type/prefetch; address is the rest.
         * Bochs adapter is always a 32-bit non-prefetch memory BAR. */
        return bar0 & 0xFFFFFFF0U;
    }
    return 0;
}

/* Map the LFB physical region into the kernel address space, sized to
 * the requested mode.  Re-uses the existing mapping when the new size
 * fits; remaps when growing.  Returns a virtual address, or NULL on
 * failure (in which case the caller should fall back to banked mode). */
static void *bga_map_lfb(uint32_t phys, size_t size) {
    if (bga_lfb_virt && size <= bga_lfb_mapped_size) {
        return bga_lfb_virt;
    }
    void *va = ioremap_wc((resource_size_t)phys, size);
    if (!va) {
        return NULL;
    }
    bga_lfb_virt = va;
    bga_lfb_mapped_size = size;
    return va;
}

#ifdef HOST_TEST
extern volatile uint8_t *bga_test_bank_window;
#define BGA_BANK_WINDOW ((volatile uint8_t *)bga_test_bank_window)
#else
#define BGA_BANK_WINDOW ((volatile uint8_t *)(uintptr_t)BGA_BANK_WINDOW_VIRT)
#endif

static int bga_force_banked_mode(void) {
    char value[16];

    if (cmdline_get("bga", value, sizeof(value)) != 0) {
        return 0;
    }
    return strcmp(value, "nolfb") == 0;
}

static uint32_t bga_pack_color(uint32_t color, uint8_t bpp) {
    switch (bpp) {
    case 32:
        return color;
    case 24:
        return color & 0x00FFFFFFU;
    case 16: {
        uint32_t r = (color >> 16) & 0xFFU;
        uint32_t g = (color >> 8) & 0xFFU;
        uint32_t b = color & 0xFFU;
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    default:
        return color;
    }
}

static void bga_select_bank(uint16_t bank) {
    if (bga_current_bank == bank) {
        return;
    }
    bga_write(VBE_DISPI_INDEX_BANK, bank);
    bga_current_bank = bank;
}

static void bga_banked_putpixel(int x, int y, uint32_t color) {
    uintptr_t offset;
    uint32_t raw;
    int bytes_per_pixel;
    int i;

    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height) {
        return;
    }

    bytes_per_pixel = (fb.bpp + 7) / 8;
    if (bytes_per_pixel <= 0) {
        return;
    }

    offset = (uintptr_t)y * fb.pitch + (uintptr_t)x * (uintptr_t)bytes_per_pixel;
    raw = bga_pack_color(color, fb.bpp);

    for (i = 0; i < bytes_per_pixel; i++) {
        uintptr_t byte_off = offset + (uintptr_t)i;
        uint16_t bank = (uint16_t)(byte_off / BGA_BANK_SIZE);
        uintptr_t in_bank = byte_off % BGA_BANK_SIZE;

        bga_select_bank(bank);
        BGA_BANK_WINDOW[in_bank] = (uint8_t)((raw >> (i * 8)) & 0xFFU);
    }
}

int bga_init(fb_info_t *fb_out) {
    uint16_t enable_flags;
    uint16_t virt_height;

    if (!bga_is_available()) {
        kprint("BGA: Device not available on I/O ports.\n");
        return -1;
    }

    /* Locate the Bochs adapter via PCI and read BAR0 — the LFB
     * physical address.  The previous hardcoded 0xE0000000 fallback
     * only worked on specific QEMU machine types; modern qemu places
     * the Bochs BAR at 0xFD000000 or higher, and writes to the wrong
     * physical region just leak into RAM (pitch black framebuffer). */
    uint32_t discovered = bga_discover_lfb_phys();
    if (discovered) {
        bga_lfb_addr = discovered;
        kprintf("BGA: LFB physical address %#x (via PCI BAR0)\n", discovered);
    } else {
        bga_lfb_addr = 0xE0000000;
        kprint("BGA: PCI enumeration miss, using fallback 0xE0000000\n");
    }
    bga_use_lfb = bga_force_banked_mode() ? 0 : 1;
    
    // Set resolution: 1024x768x32
    uint16_t width = 1024;
    uint16_t height = 768;
    virt_height = bga_use_lfb ? (uint16_t)(height * 2) : height;
    uint16_t bpp = 32;

    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, width);
    bga_write(VBE_DISPI_INDEX_YRES, height);
    bga_write(VBE_DISPI_INDEX_BPP, bpp);
    bga_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, virt_height);
    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    enable_flags = VBE_DISPI_ENABLED;
    if (bga_use_lfb) {
        enable_flags |= VBE_DISPI_LFB_ENABLED;
    }
    bga_write(VBE_DISPI_INDEX_ENABLE, enable_flags);
    bga_current_bank = 0xFFFFU;

    /* If using LFB, ioremap the physical aperture so the kernel can
     * actually touch it — bga_lfb_addr is the PHYSICAL address (PCI
     * BAR0 on the Bochs adapter), not a kernel virtual address.  Map
     * pitch*virt_height bytes so hardware-scroll has its full virtual
     * canvas.  If the mapping fails, fall back to banked mode. */
    void *lfb_va = NULL;
    if (bga_use_lfb) {
        size_t pitch_bytes = (size_t)width * (size_t)bpp / 8U;
        size_t lfb_size = pitch_bytes * (size_t)virt_height;
        lfb_va = bga_map_lfb(bga_lfb_addr, lfb_size);
        if (!lfb_va) {
            kprint("BGA: ioremap of LFB failed, falling back to banked mode.\n");
            bga_use_lfb = 0;
            /* Reprogram without LFB enable. */
            bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
            bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED);
            bga_current_bank = 0xFFFFU;
        }
    }

    // Update fb info
    fb_out->addr = bga_use_lfb ? (uint32_t *)lfb_va
                               : (uint32_t *)(uintptr_t)BGA_BANK_WINDOW_VIRT;
    /* phys: what userspace mmap(/dev/fb0) PTEs must point at.  LFB
     * mode uses the PCI BAR physical address; banked mode has no
     * direct user-mmap path so leave phys=0 (mmap will refuse). */
    fb_out->phys = bga_use_lfb ? (uintptr_t)bga_lfb_addr : 0;
    fb_out->width = width;
    fb_out->height = height;
    fb_out->virt_height = virt_height;
    fb_out->bpp = bpp;
    fb_out->pitch = (width * bpp) / 8;
    fb_out->virt_width = width;
    fb_out->virt_height = virt_height;
    fb_out->set_viewport = bga_use_lfb ? bga_set_viewport : NULL;
    fb_out->putpixel = bga_use_lfb ? linear_fb_putpixel : bga_banked_putpixel;

    /* Implement Hardware Scroll */
    fb_out->scroll = bga_use_lfb ? bga_scroll : NULL;

    kprint(bga_use_lfb
               ? "BGA: Mode set to 1024x768x32 with Hardware Scrolling.\n"
               : "BGA: Mode set to 1024x768x32 using banked fallback.\n");
    return 0;
}

static int bga_set_viewport(int x, int y) {
    if (!bga_use_lfb) {
        return -1;
    }
    bga_write(VBE_DISPI_INDEX_X_OFFSET, (uint16_t)x);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, (uint16_t)y);
    return 0;
}

void bga_scroll(int y_offset) {
    if (!bga_use_lfb) {
        return;
    }
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, (uint16_t)y_offset);
}

static struct video_mode_info bga_modes[] = {
    { 640, 480, 32, 1, 0, {0} },
    { 800, 600, 32, 2, 0, {0} },
    { 1024, 768, 32, 3, 0, {0} },
    { 1280, 720, 32, 4, 0, {0} },
    { 1280, 1024, 32, 5, 0, {0} }
};
#define BGA_MODE_COUNT (int)(sizeof(bga_modes) / sizeof(bga_modes[0]))

static int bga_list_modes(struct video_mode_info *modes, int max_count) {
    if (!modes) return BGA_MODE_COUNT;
    int count = 0;
    for (int i = 0; i < BGA_MODE_COUNT && count < max_count; i++) {
        modes[count] = bga_modes[i];
        count++;
    }
    return count;
}

static int bga_set_mode(int mode_id) {
    /* Find the mode by ID */
    const struct video_mode_info *mode = NULL;
    for (int i = 0; i < BGA_MODE_COUNT; i++) {
        if ((int)bga_modes[i].mode_id == mode_id) {
            mode = &bga_modes[i];
            break;
        }
    }
    if (!mode) return -1;

    uint16_t width = (uint16_t)mode->width;
    uint16_t height = (uint16_t)mode->height;
    uint16_t bpp = (uint16_t)mode->bpp;
    uint16_t virt_height = bga_use_lfb ? (uint16_t)(height * 2) : height;
    uint16_t enable_flags = VBE_DISPI_ENABLED;

    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, width);
    bga_write(VBE_DISPI_INDEX_YRES, height);
    bga_write(VBE_DISPI_INDEX_BPP, bpp);
    bga_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, virt_height);
    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    if (bga_use_lfb) {
        enable_flags |= VBE_DISPI_LFB_ENABLED;
    }
    bga_write(VBE_DISPI_INDEX_ENABLE, enable_flags);
    bga_current_bank = 0xFFFFU;

    /* Map / extend the LFB aperture into the kernel address space.  See
     * bga_init() for the same logic.  Falls back to banked mode if the
     * mapping can't be set up. */
    void *lfb_va = NULL;
    if (bga_use_lfb) {
        size_t pitch_bytes = (size_t)width * (size_t)bpp / 8U;
        size_t lfb_size = pitch_bytes * (size_t)virt_height;
        lfb_va = bga_map_lfb(bga_lfb_addr, lfb_size);
        if (!lfb_va && virt_height > height) {
            /* The doubled (hardware-scroll) aperture didn't fit the bounded
             * ioremap pool — a high-res mode's 2x virtual height can be 7+ MiB.
             * Retry a single buffer: still a *linear* LFB (so the console
             * shadow and userspace /dev/fb0 mmap work and stay fast), just
             * without the hardware vertical pan.  Far better than dropping to
             * banked mode, which repaints the console a character at a time. */
            virt_height = height;
            lfb_size = pitch_bytes * (size_t)virt_height;
            bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, virt_height);
            lfb_va = bga_map_lfb(bga_lfb_addr, lfb_size);
            if (lfb_va) {
                kprint("BGA: LFB hardware-scroll aperture too large for the "
                       "ioremap pool; using a single linear buffer.\n");
            }
        }
        if (!lfb_va) {
            kprint("BGA: ioremap of LFB failed, banked mode active.\n");
            bga_use_lfb = 0;
            bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
            bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED);
            bga_current_bank = 0xFFFFU;
        }
    }

    /* Update global fb */
    fb.addr = bga_use_lfb ? (uint32_t *)lfb_va
                          : (uint32_t *)(uintptr_t)BGA_BANK_WINDOW_VIRT;
    fb.width = width;
    fb.height = height;
    fb.bpp = bpp;
    fb.pitch = (width * bpp) / 8;
    fb.virt_width = width;
    fb.virt_height = virt_height;
    fb.set_viewport = bga_use_lfb ? bga_set_viewport : NULL;
    fb.scroll = bga_use_lfb ? bga_scroll : NULL;
    fb.putpixel = bga_use_lfb ? linear_fb_putpixel : bga_banked_putpixel;

    return 0;
}

static video_driver_t bga_driver = {
    .name = "bga",
    .priority = 50,
    .probe = bga_probe_driver,
    .init = bga_init,
    .set_viewport = bga_set_viewport,
    .list_modes = bga_list_modes,
    .set_mode = bga_set_mode
};

void bga_install(void) {
    video_register_driver(&bga_driver);
}
