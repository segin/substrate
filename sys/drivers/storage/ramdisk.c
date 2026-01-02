#include <sys/types.h>
#include <sys/types.h>
#include "../../kern/console.h"
#include <string.h>
#include <string.h>

// Simple RAM Disk Block Driver
// Registers as a block device (TODO: Block Device Interface)
// For now, just stores data and size.

static void *ramdisk_addr = NULL;
static size_t ramdisk_size = 0;

void ramdisk_init(void *addr, size_t size) {
    if (!addr || size == 0) return;
    
    ramdisk_addr = addr;
    ramdisk_size = size;
    
    kprint("RAM Disk initialized at ");
    // kprintf("%p, size: %d bytes\n", addr, size); // TODO: kprintf
    kprint("(address), size: ");
    // Convert size to string manually since we don't have kprintf yet
    char buf[32];
    size_t n = size;
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    buf[i] = 0;
    // Reverse
    for(int j=0; j<i/2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i-1-j];
        buf[i-1-j] = tmp;
    }
    kprint(buf);
    kprint(" bytes.\n");
    
    // Allow VFS to mount it?
    // Need a special filesystem driver for Initrd?
    // Typically initrd acts as a block device for Ext2/Minix, OR as a CPIO archive (tmpfs).
    // Linux initrd (cpio) is unpacked into rootfs (ramfs).
    // Linux initrd (image) is mounted as block device.
    
    // For now, we just log it.
}
