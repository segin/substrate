#include <stdint.h>
#include "../drivers/video/vga.h"
#include "../drivers/serial/uart.h"
#include "../drivers/input/keyboard.h"
#include "../drivers/input/mouse.h"
#include "../drivers/storage/scsi/scsi.h"
#include "../drivers/storage/ide/ide.h"
#include "../drivers/storage/ahci/ahci.h"
#include "../drivers/storage/nvme/nvme.h"
#include "../arch/i386/idt.h"
#include "../arch/i386/gdt.h"
#include "../arch/i386/pmm.h"
#include "../arch/i386/pmap.h"
#include "../arch/i386/pci.h"
#include "../arch/i386/syscall.h"
#include "../arch/i386/fpu/fpu_emu.h"
#include "../arch/i386/multiboot.h"
#include "sched.h"
#include "../vfs/vfs.h"
#include "../fs/ext2/ext2.h"
#include "../fs/fat/fat.h"
#include "../fs/exfat/exfat.h"
#include "../fs/minix/minix.h"
#include "version.h"
#include "panic.h"

// Simple string functions to avoid depending on libc in core if not available
static int k_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int k_strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static size_t k_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

// Try to execute init
static int try_init(const char *path) {
    // In a real OS, we would do:
    // sys_execve(path, argv, envp);
    
    // Check if file exists
    // We assume fs_root is mounted.
    // If fs_root is NULL (which it is currently until we mount root), we can't look it up.
    if (!fs_root) return -1;

    fs_node_t *node = finddir_fs(fs_root, (char*)path + 1); // Skip leading /
    if (node) {
        vga_write("Found init: ", 12);
        vga_write(path, k_strlen(path));
        vga_write("\n", 1);
        
        // Mock execution
        // sys_execve(path, ...);
        return 0;
    }
    return -1;
}

void init_task(void *arg) {
    char *cmdline = (char*)arg;
    char *init_path = NULL;

    // Parse cmdline for init=
    if (cmdline) {
        char *p = cmdline;
        while (*p) {
            if (k_strncmp(p, "init=", 5) == 0) {
                init_path = p + 5;
                // Find end of path (space or end of string)
                char *end = init_path;
                while (*end && *end != ' ') end++;
                *end = 0; // Terminate it temporarily (dangerous if other things need cmdline, but ok here)
                break;
            }
            p++;
        }
    }

    if (init_path) {
        if (try_init(init_path) == 0) return;
        // If explicit init fails, should we fallback? 
        // Linux behavior: "Kernel panic - not syncing: Requested init ... failed".
        panic("Requested init failed.");
    }

    // Default paths
    if (try_init("/sbin/init") == 0) return;
    if (try_init("/etc/init") == 0) return;
    if (try_init("/bin/init") == 0) return;
    if (try_init("/bin/sh") == 0) return; // Fallback to shell often helpful

    panic("No init found. Try passing init= option to kernel.");
}

// Kernel Entry Point
void kmain(unsigned long magic, unsigned long addr) {
    vga_init();
    uart_init();
    
    vga_write("Kernel Started: ", 16);
    vga_write(OS_NAME, sizeof(OS_NAME) - 1);
    vga_write("\n", 1);
    
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        panic("Invalid multiboot magic.");
    }

    multiboot_info_t *mboot_info = (multiboot_info_t*)addr;
    char *cmdline = NULL;
    if (mboot_info->flags & (1<<2)) {
        cmdline = (char*)mboot_info->cmdline;
        vga_write("Cmdline: ", 9);
        vga_write(cmdline, k_strlen(cmdline));
        vga_write("\n", 1);
    }
    
    // Initialize PMM using Multiboot mmap
    if (mboot_info->flags & (1<<6)) {
        pmm_init(mboot_info->mmap_addr, mboot_info->mmap_length);
        vga_write("PMM Initialized with Multiboot mmap.\n", 37);
    } else {
        // Fallback or panic
        pmm_init(0, 0); 
        vga_write("PMM Initialized (no mmap).\n", 27);
    }

    // Initialize GDT
    gdt_init();
    vga_write("GDT Initialized.\n", 17);

    // Initialize IDT
    idt_init();
    vga_write("IDT Initialized.\n", 17);

    // Initialize PMAP (Paging)
    pmap_bootstrap();
    
    // Initialize FPU
    fpu_init();
    
    // Initialize Keyboard
    keyboard_init();
    
    // Initialize Mouse
    mouse_init();

    // Initialize Storage
    scsi_init();
    ide_init();
    ahci_init();
    nvme_init();
    
    // Initialize Syscalls
    syscall_init();

    // Initialize PCI
    pci_init();

    // Initialize VFS & Filesystems
    ext2_init();
    fat_init();
    exfat_init();
    minix_init();
    vfs_init_mock_root(); // Hack for init finding
    
    // Initialize Scheduler
    sched_init();
    vga_write("Scheduler Initialized.\n", 23);

    // Create Init Task
    // We pass cmdline to it
    static char dummy_stack[4096];
    sched_create_thread(current_process, init_task, dummy_stack + 4096, cmdline);

    vga_write("Entering main loop...\n", 22);
    while (1) {
        sched_yield();
        // Simple delay loop
        for(volatile int i=0; i<100000; i++); 
    }
}
