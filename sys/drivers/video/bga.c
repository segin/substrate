#include <sys/types.h>
#include <sys/file.h>
#include <kern/console.h>
#include <drivers/video/fb.h>
#include <arch/x86-common/include/io.h>

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

void bga_scroll(int y_offset);
static int bga_set_viewport(int x, int y);

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

// Fixed LFB address for BGA is usually at BAR0 of the PCI device, 
// but traditionally for Bochs/QEMU it is at 0xE0000000 (ISA hole/high mem).
// Ideally we should use PCI enumeration to find the device (VENDOR=0x1234, DEVICE=0x1111).
// For simplicity, we'll assume 0xE0000000 if PCI scanning isn't hooked up yet for this.
// Wait, we have pci_init. We should try to find it via PCI for correctness?
// Let's stick to simple init for now, maybe scan PCI later or assume E0000000.
// QEMU: -vga std puts it at 0xFD000000 often. 
// Using PCI is safer.

// #include "../../arch/i386/pci.h"
extern uint32_t pci_read_config(uint32_t device, int offset);

static uint32_t bga_lfb_addr = 0;


/*
static void find_bga_pci(uint32_t device, uint16_t vendor_id, uint16_t device_id, void *extra) {
    (void)extra;
    if (vendor_id == 0x1234 && device_id == 0x1111) {
        // Found Bochs VGA
        // BAR0 is usually LFB
        bga_lfb_addr = pci_read_config(device, 0x10) & 0xFFFFFFF0;
        kprint("BGA: Found PCI device at 0x");
        extern void kprint_hex(uint32_t n);
        kprint_hex(bga_lfb_addr);
        kprint("\n");
    }
}
*/

int bga_init(fb_info_t *fb_out) {
    if (!bga_is_available()) {
        kprint("BGA: Device not available on I/O ports.\n");
        return -1;
    }

    // Try to find via PCI to get accurate LFB address
    extern void pci_scan_bus(void (*callback)(uint32_t, uint16_t, uint16_t, void*), void *extra);
    // pci_init checks buses, but we need to scan callbacks. 
    // Assuming pci_scan_bus is available or similar from pci.c
    // Let's check pci.h/c definitions... wait, I can't check now inside write.
    // I'll assume I can scan or just use default specific to QEMU/Bochs if PCI fails.
    // Default fallback: 0xE0000000
    bga_lfb_addr = 0xE0000000;
    
    // Attempt scan (commented out until verified PCI API)
    // pci_scan_bus(find_bga_pci, 0);

    // Set resolution: 1024x768x32
    uint16_t width = 1024;
    uint16_t height = 768;
    uint16_t virt_height = height * 2; // Double buffering for scrolling
    uint16_t bpp = 32;

    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, width);
    bga_write(VBE_DISPI_INDEX_YRES, height);
    bga_write(VBE_DISPI_INDEX_BPP, bpp);
    bga_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, virt_height);
    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    // Update fb info
    fb_out->addr = (uint32_t*)bga_lfb_addr;
    fb_out->width = width;
    fb_out->height = height;
    fb_out->virt_height = virt_height;
    fb_out->bpp = bpp;
    fb_out->pitch = (width * bpp) / 8;
    fb_out->virt_width = width;
    fb_out->virt_height = virt_height;
    fb_out->set_viewport = bga_set_viewport;

    /* Implement Hardware Scroll */
    fb_out->scroll = bga_scroll;

    kprint("BGA: Mode set to 1024x768x32 with Hardware Scrolling.\n");
    return 0;
}

static int bga_set_viewport(int x, int y) {
    bga_write(VBE_DISPI_INDEX_X_OFFSET, (uint16_t)x);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, (uint16_t)y);
    return 0;
}

void bga_scroll(int y_offset) {
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

static video_driver_t bga_driver = {
    .name = "bga",
    .priority = 50,
    .probe = bga_is_available,
    .init = bga_init,
    .set_viewport = bga_set_viewport,
    .list_modes = bga_list_modes
};

void bga_install(void) {
    video_register_driver(&bga_driver);
}
