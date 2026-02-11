#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/proc.h>
#include <sys/input.h>
#include <arch/i386/early_boot.h>

#include <drivers/video/vga.h>
#include <drivers/video/fb.h>
#include <drivers/video/hw_text.h>
#include <drivers/console/uart/uart.h>
#include <drivers/input/keyboard.h>
#include <drivers/input/mouse.h>
#include <drivers/storage/scsi/scsi.h>
#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ahci/ahci.h>
#include <drivers/storage/nvme/nvme.h>
#include <drivers/virtio/virtio.h>
#include <drivers/storage/ramdisk.h>

#include <arch/i386/idt.h>
#include <arch/i386/gdt.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pci.h>
#include <arch/i386/syscall.h>
#include <arch/i386/fpu/fpu_emu.h>
#include <arch/x86-common/include/rtc.h>
#include <arch/x86-common/include/multiboot.h>

#include <pm/pm.h>
#include <sys/crc32.h>
#include <vfs/vfs.h>
#include <exec/formats/elf.h>
#include <fs/procfs.h>
#include <fs/sysfs.h>
#include <fs/pseudofs.h>
#include <fs/fuse.h>
#include <fs/9p.h>

#include <sys/tests.h>
#include <sys/smp.h>

extern void ntsync_init(void);


#include <kern/console.h>
#include <kern/cmdline.h>
#include <kern/sched.h>
#include <kern/version.h>
#include <kern/panic.h>

// Simple string functions to avoid depending on libc in core if not available
int serial_debug_enabled = 0;
int syscall_trace_enabled = 0;
char kernel_hostname[MAXHOSTNAMELEN] = "localhost";

// Multiboot Data Reclamation Support
static multiboot_info_t mboot_copy;
static multiboot_mmap_entry_t mboot_mmap_copy[64];
static struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} mboot_mods_copy[8];
static uint32_t mboot_orig_addr = 0;

// 1. Address Translation Macros (since we are Higher Half)
#define PHYSICAL_d(x) ((uint32_t)(x) - 0xC0000000)
#define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + 0xC0000000))

static void init_memory(multiboot_info_t *mboot_info) {
    uint32_t mmap_addr = 0;
    uint32_t mmap_length = 0;

    // Dump Memory Map Early
    if (mboot_info && (mboot_info->flags & (1<<6))) {
        pmm_dump_mmap((uintptr_t)VIRTUAL_d(mboot_info->mmap_addr), mboot_info->mmap_length);
    }

    if (mboot_info && (mboot_info->flags & (1<<6))) {
        mmap_addr = (uintptr_t)VIRTUAL_d(mboot_info->mmap_addr);
        mmap_length = mboot_info->mmap_length;
        
        // Copy mmap for reclamation safety
        uint32_t count = mmap_length / sizeof(multiboot_mmap_entry_t);
        if (count > 64) count = 64;
        memcpy(mboot_mmap_copy, (void*)(uintptr_t)mmap_addr, count * sizeof(multiboot_mmap_entry_t));
        mboot_copy.mmap_addr = PHYSICAL_d(mboot_mmap_copy);
        mboot_copy.mmap_length = count * sizeof(multiboot_mmap_entry_t);
        mboot_copy.flags |= (1<<6);
    }

    // Initialize PMM
    if (mmap_addr) {
        pmm_init(mmap_addr, mmap_length);
        kprint("PMM Initialized with Multiboot mmap.\n");
    } else {
        pmm_init(0, 0); 
        kprint("PMM Initialized (no mmap).\n");
    }

    // Initialize VM subsystem
    extern void vm_page_init(void);
    extern void vm_object_init(void);
    extern void vm_zone_init(void);
    extern void uma_startup(void);
    extern void kmem_init(void);
    vm_page_init();
    vm_object_init();
    vm_zone_init();

    // Discover Cores before UMA startup so UMA can init per-CPU caches
    smp_discover_cores();

    uma_startup();    // Initialize UMA before kmem (kmem uses UMA zones)
    kmem_init();      // Initialize kernel memory allocator
    kprint("VM subsystem initialized.\n");
    
    // Update mboot_info global copy if needed
    if (mboot_info) {
        memcpy(&mboot_copy, mboot_info, sizeof(multiboot_info_t));
        if (mboot_info->flags & (1<<3)) {
             uint32_t mods_count = mboot_info->mods_count;
             if (mods_count > 8) mods_count = 8;
             uint32_t mods_addr_virt = (uintptr_t)VIRTUAL_d(mboot_info->mods_addr);
             memcpy(mboot_mods_copy, (void*)(uintptr_t)mods_addr_virt, mods_count * 16);
             mboot_copy.mods_addr = PHYSICAL_d(mboot_mods_copy);
             mboot_copy.mods_count = mods_count;
        }
    }
}


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


int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}



static void init_root_fs(void) {
    // Parse root= argument
    char root_dev[64] = {0};
    if (cmdline_get("root", root_dev, sizeof(root_dev)) == 0) {
        kprint("Mounting root from: ");
        kprint(root_dev);
        kprint("\n");
        
        char root_type[32] = {0};
        if (cmdline_get("rootfstype", root_type, sizeof(root_type)) != 0) {
             strcpy(root_type, "ext2");
        }
        
        if (vfs_mount_legacy(root_dev, "/", root_type, 0, NULL) != 0) {
            kprint("VFS: Cannot mount root ");
            kprint(root_dev);
            kprint("\n");
            
            kprint("VFS: Trying /dev/storage/ram0\n");
            if (vfs_mount_legacy("/dev/storage/ram0", "/", "ext2", 0, NULL) != 0) {
                 panic("not syncing - cannot mount root!");
            }
        }
    } else {
        kprint("VFS: No root= argument specified.\n");
        kprint("VFS: Trying /dev/storage/ram0\n");
        if (vfs_mount_legacy("/dev/storage/ram0", "/", "ext2", 0, NULL) != 0) {
            panic("not syncing - cannot mount root!");
        }
    }

    if (!fs_root) {
        panic("not syncing - cannot mount root!");
    }

    // Mount pseudo-filesystems AFTER root is established
    vfs_mount_legacy(NULL, "/dev", "devfs", 0, NULL);
    vfs_mount_legacy(NULL, "/proc", "procfs", 0, NULL);
    vfs_mount_legacy(NULL, "/sys", "sysfs", 0, NULL);
}

// kinit - kernel init task (becomes PID 1 after exec)
// This is forked from kernel task 0 and execs the init binary
void kinit_task(void *arg) {
    (void)arg;  // Unused now that we use cmdline_get
    char *init_path = NULL;
    
    kprint("kinit: Starting init process...\n");
    
    // Create new session for init (PID 1)
    extern int sys_setsid(void);
    if (sys_setsid() < 0) {
        kprint("kinit: sys_setsid failed!\n");
    }

    // Attach stdin/stdout/stderr to console directly
    console_attach_std_fds(current_process);

    // Parse cmdline for init= using proper cmdline API
    char init_buf[256];
    if (cmdline_get("init", init_buf, sizeof(init_buf)) == 0) {
        init_path = init_buf;
        kprint("kinit: Trying ");
        kprint(init_path);
        kprint("\n");
        if (elf_execve(init_path, NULL, NULL) == 0) {
            goto exec_success;
        }
        panic("kinit: Requested init failed.");
    }

    // Default paths
    kprint("kinit: Trying default init paths...\n");
    if (elf_execve("/sbin/init", NULL, NULL) == 0) goto exec_success;
    if (elf_execve("/etc/init", NULL, NULL) == 0) goto exec_success;
    if (elf_execve("/bin/init", NULL, NULL) == 0) goto exec_success;
    if (elf_execve("/bin/sh", NULL, NULL) == 0) goto exec_success;

    panic("kinit: No init found. Try passing init= option to kernel.");

exec_success:
    // exec succeeded (or will when usermode is implemented)
    // In a real kernel, we wouldn't reach here - we'd jump to userspace
    kprint("kinit: exec returned (usermode not implemented)\n");
    kprint("System idle - init loaded but cannot run.\n");
    for (;;) { __asm__ volatile("hlt"); }
}

// Legacy init_task - redirects to kinit_task
void init_task(void *arg) {
    kinit_task(arg);
}



// Kernel Entry Point
void kmain(unsigned long magic, unsigned long addr) {
    /* Initialize early GDT and IDT FIRST to catch any faults */
    early_gdt_init();
    early_idt_init();
    
    early_uart_print("KMAIN START\n");
    mboot_orig_addr = addr;
    // 0. Setup Kernel Task IMMEDIATELY
    early_uart_print("KMAIN: pm_init\n");
    pm_init();
    current_process = &processes[0];
    current_process->pid = 0;
    strcpy(current_process->comm, "(swapper)");

    // 2. Process Multiboot Info EARLY to get cmdline
    // Note: boot.S already converted 'addr' (mboot_info) to Higher Half Virtual Address.
    // However, pointers *inside* the struct (like cmdline, mmap_addr) are still PHYSICAL.
    multiboot_info_t *mboot_info = (multiboot_info_t *)addr;
    static multiboot_info_t fake_mbi;
    char *cmdline = NULL;

    if (magic == 0xF8EEB5D0) {
        memset(&fake_mbi, 0, sizeof(fake_mbi));
        mboot_info = &fake_mbi;
         kprint("Booted via FreeBSD loader.\n");
    } else if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        if (mboot_info->flags & (1<<2)) {
            // cmdline pointer is physical, convert to virtual
            cmdline = (char *)VIRTUAL_d(mboot_info->cmdline);
        }
    } else {
         kprint("Warning: Unknown bootloader magic, assuming raw boot.\n");
         mboot_info = NULL; 
    }

    // 4. Initialize Command Line Parser
    if (cmdline) {
        cmdline_init(cmdline);
    } else {
        cmdline_init("");
    }
    kprint("\n");


    // Memory Subsystem Init (PMM, VM, UMA)
    init_memory(mboot_info);

    console_init();
    hw_text_init(); // Registers VGA text console
    uart_init(); // Initializes UART hardare

    // Check for serial debug (MUST be after console_init which clears backends)
    if (cmdline_has("serial_debug")) {
        serial_debug_enabled = 1;
        console_register(uart_get_console());
        kprint("Serial Debug Enabled.\n");
    }
    
    if (cmdline_has("syscall_trace") || cmdline_has("syscall_log")) {
        syscall_trace_enabled = 1;
        kprint("Syscall Tracing Enabled.\n");
    }

    // Display kernel ident banner (mirrored if serial_debug_enabled)
    kprint(OS_NAME " kernel v" OS_VERSION " (i386)\n");

    // Initialize GDT
    gdt_init();
    kprint("GDT Initialized.\n");

    // Initialize IDT immediately after GDT to catch early exceptions
    idt_init();
    
    // Initialize FPU (needs IDT for #NM handler)
    extern void fpu_init(void);
    fpu_init();

    // Initialize RTC and set system time
    rtc_init();

    // Initialize PMAP (Paging) - maps LAPIC and sets up recursive paging
    pmap_bootstrap();
    
    // Map Signal Trampoline Page (VDSO)
    extern void pmap_map_trampoline(void);
    pmap_map_trampoline();
    
    // Initialize Random Number Generator
    extern void random_init(void);
    random_init();

    // Initialize CRC32 table (used by storage/GPT)
    crc32_init();

    // Initialize Scheduler
    sched_init();
    kprint("Scheduler Initialized.\n");

    // Initialize Sysctl Subsystem
    extern void sysctl_init(void);
    sysctl_init();
    extern void sysctl_init(void);
    sysctl_init();
    
    keyboard_init();
    
    // Initialize Framebuffer Console (if available) - Defer until paging is up
    if (mboot_info) {
        fb_init(mboot_info);
    }
    

    // Diagnostic: Print command line
    char full_cmd[512] = {0};
    if (cmdline_get_full(full_cmd, sizeof(full_cmd)) == 0) {
        kprint("Boot Args: ");
        kprint(full_cmd);
        kprint("\n");
    }
    
    if (serial_debug_enabled) {
        kprint("Serial Debug Enabled.\n");
    }
    
    kprint("IDT Initialized.\n");

    // 4. Hardware Discovery - PCI Bus Scan, Storage Controllers
    pci_init();
    ide_init();
    virtio_init();
    ntsync_init();
    
    // Run Kernel Tests (if requested via cmdline 'test=...')
    run_kernel_tests();
    
    // Initialize VFS (handles filesystem registration and pseudo-fs mounts)
    vfs_init();
    
    // Register console device in /dev
    console_register_devfs();
    
    // Mount Root Filesystem
    init_root_fs();

    // Create Init Task
    // We pass cmdline to it
    sched_spawn_kernel_process(init_task, cmdline);

    // Reclaim early boot code
    pmm_reclaim_setup();
    
    // Reclaim original multiboot info (1 page)
    // We want to reclaim the ORIGINAL mods_addr from the bootloader.
    // We must read it from the MBI *before* we reclaim the MBI memory!
    
    // Note: mboot_orig_addr is already Virtual (passed from boot.S)
    multiboot_info_t *orig_mbi = (multiboot_info_t*)mboot_orig_addr;
    uint32_t mods_addr = 0;
    int has_mods = 0;
    
    if (mboot_orig_addr) {
        if (orig_mbi->flags & (1<<3)) {
            has_mods = 1;
            mods_addr = orig_mbi->mods_addr;
        }
        
        kprint("Freeing Multiboot info: 4K\n");
        pmm_reclaim_range(mboot_orig_addr, mboot_orig_addr + 4096);
    }
    
    if (has_mods) {
        kprint("Freeing Multiboot modules list: 4K\n");
        pmm_reclaim_range(mods_addr, mods_addr + 4096);
    }

    kprint("Entering main loop...\n");
    while (1) {
        sched_yield();
        // Halt until next interrupt (power efficient idle)
        __asm__ volatile("sti; hlt");
    }
}
