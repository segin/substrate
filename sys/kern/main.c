#include <stdint.h>
#include "../drivers/video/vga.h"
#include "../drivers/video/fb.h"
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
#include <sys/input.h>
#include "../vfs/vfs.h"
#include "version.h"
#include "panic.h"
#include "console.h"
#include "cmdline.h"
#include <string.h>

// Simple string functions to avoid depending on libc in core if not available
int serial_debug_enabled = 0;


char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
             const char *h = haystack;
             const char *n = needle;
             while (*h && *n && *h == *n) {
                 h++; n++;
             }
             if (!*n) return (char*)haystack;
        }
    }
    return NULL;
}


static int k_strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}



// Try to execute init
static int try_init(const char *path) {
    // In a real OS, we would do:
    // sys_execve(path, argv, envp);
    
    // Check if file exists
    // We assume fs_root is mounted.
    if (!fs_root) return -1;

    // Use vfs_lookup to traverse the path (init path is absolute)
    fs_node_t *node = vfs_lookup(fs_root, path);
    if (node) {
        kprint("Found init: ");
        kprint(path);
        kprint("\n");
        
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

extern void pseudo_init(void);
extern void procfs_init(void);
extern void sysfs_init(void);
extern void fuse_init(void);
extern void fuse_fs_init(void);
extern void p9_init(void);

// Kernel Entry Point
void kmain(unsigned long magic, unsigned long addr) {
    console_init();
    vga_init(); // Registers VGA console
    uart_init(); // Initializes UART hardare

    // 1. Process Multiboot Info EARLY to get cmdline
    multiboot_info_t *mboot_info = (multiboot_info_t *)addr;
    static multiboot_info_t fake_mbi;
    char *cmdline = NULL;

    if (magic == 0xF8EEB5D0) {
        memset(&fake_mbi, 0, sizeof(fake_mbi));
        mboot_info = &fake_mbi;
         kprint("Booted via FreeBSD loader.\n");
    } else if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        if (mboot_info->flags & (1<<2)) {
            cmdline = (char *)mboot_info->cmdline;
        }
    } else {
         kprint("Warning: Unknown bootloader magic, assuming raw boot.\n");
         mboot_info = NULL; 
    }

    // 2. Initialize Command Line Parser
    if (cmdline) {
        cmdline_init(cmdline);
    } else {
        cmdline_init("");
    }

    // Check for serial debug
    if (cmdline_has("serial_debug")) {
        serial_debug_enabled = 1;
        console_register(uart_get_console());
        kprint("Serial Debug Enabled.\n");
    }

    // Display kernel ident banner (mirrored if serial_debug_enabled)
    kprint(OS_NAME " kernel v" OS_VERSION " (i386)\n");

    uint32_t mmap_addr = 0;
    uint32_t mmap_length = 0;
    
    // Parse Multiboot Modules (Initrd)
    if (mboot_info && (mboot_info->flags & (1<<3))) {
        // Mods present
        uint32_t mods_count = mboot_info->mods_count;
        uint32_t mods_addr = mboot_info->mods_addr;
        
        kprint("Multiboot Modules found: ");
        // Print count
        char c = mods_count + '0'; // Hack for single digit
        char s[2] = {c, 0};
        kprint(s);
        kprint("\n");
        
        if (mods_count > 0) {
            // Assume first module is initrd
            // module structure: 
            // u32 mod_start
            // u32 mod_end
            // u32 string (cmdline)
            // u32 reserved
            struct multiboot_mod_list {
                uint32_t mod_start;
                uint32_t mod_end;
                uint32_t cmdline;
                uint32_t pad;
            } *mod = (struct multiboot_mod_list *)mods_addr;
            
            extern void ramdisk_init(void *addr, size_t size);
            ramdisk_init((void*)mod->mod_start, mod->mod_end - mod->mod_start);
        }
    }
    
    if (mboot_info && (mboot_info->flags & (1<<6))) {
        mmap_addr = mboot_info->mmap_addr;
        mmap_length = mboot_info->mmap_length;
    }

    // Initialize PMM
    // We already have memory map from Multiboot
    if (mmap_addr) {
        pmm_init(mmap_addr, mmap_length);
        kprint("PMM Initialized with Multiboot mmap.\n");
    } else {
        // Fallback or panic
        pmm_init(0, 0); 
        kprint("PMM Initialized (no mmap).\n");
    }

    // Initialize GDT
    gdt_init();
    kprint("GDT Initialized.\n");

    // Initialize Scheduler BEFORE IDT (timer calls sched_yield!)
    sched_init();
    kprint("Scheduler Initialized.\n");

    // Initialize IDT (enables timer which calls sched_yield)
    idt_init();
    kprint("IDT Initialized.\n");

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

    // Initialize Input Subsystem
    input_init();

    // Initialize PCI
    pci_init();

    // Initialize VFS (handles filesystem registration and pseudo-fs mounts)
    vfs_init();
    
    // Check for Root Filesystem
    // Parse root= argument
    char root_dev[64] = {0};
    if (cmdline_get("root", root_dev, sizeof(root_dev))) {
        kprint("Mounting root from: ");
        kprint(root_dev);
        kprint("\n");
        
        // TODO: Wait for device to appear if needed?
        // Simple loop?
        
        // Try to mount as specific types or auto-detect if we had a list.
        // For now, let's look for rootfstype=
        char root_type[32] = {0};
        if (!cmdline_get("rootfstype", root_type, sizeof(root_type))) {
             // Default to ext2 if not specified, or try multiple?
             strcpy(root_type, "ext2");
        }
        
        if (vfs_mount(root_dev, "/", root_type, 0, NULL) != 0) {
            // Try fat?
            if (vfs_mount(root_dev, "/", "fat", 0, NULL) != 0) {
                 // Try v9fs?
                 // vfs_mount(root_dev, "/", "9p", 0, NULL);
                 kprint("Failed to mount root filesystem.\n");
            }
        }
    } else {
        kprint("No root= argument specified. Trying default /dev/ram0 (if initrd loaded)...\n");
        // Fallback to initrd
        vfs_mount("/dev/ram0", "/", "ext2", 0, NULL);
    }

    if (!fs_root) {
        panic("VFS: No root filesystem mounted! Use root=/dev/x argument.");
    }

    // Create Init Task
    // We pass cmdline to it
    static char dummy_stack[4096];
    extern process_t processes[]; // Accessible from sched.c
    sched_create_thread(&processes[0], init_task, dummy_stack + 4096, cmdline);

    kprint("Entering main loop...\n");
    while (1) {
        sched_yield();
        // Simple delay loop
        for(volatile int i=0; i<100000; i++); 
    }
}
