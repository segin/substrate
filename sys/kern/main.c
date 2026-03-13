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
#include <sys/tty.h>
#include <sys/vt.h>
#include <drivers/input/keyboard.h>
#include <drivers/input/mouse.h>
#include <drivers/storage/scsi/scsi.h>
#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ahci/ahci.h>
#include <drivers/storage/nvme/nvme.h>
#include <drivers/virtio/virtio.h>
#include <drivers/storage/ramdisk.h>

#include <arch/i386/idt.h>
#include <arch/i386/cpu.h>
#include <arch/i386/gdt.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pci.h>
#include <arch/i386/percpu.h>
#include <arch/i386/smp.h>
#include <arch/i386/syscall.h>
#include <arch/i386/fpu/fpu_emu.h>
#include <arch/x86-common/lapic.h>
#include <arch/x86-common/rtc.h>
#include <arch/x86-common/multiboot.h>
#include <sys/freebsd_boot.h>
#include <kern/isa.h>

#include <sys/param.h>
#include <pm/pm.h>
#include <sys/crc32.h>
#include <vm/vm_page.h>
#include <vfs/vfs.h>
#include <sys/exec.h>
#include <sys/kern_syscalls.h>
#include <exec/formats/elf.h>
#include <fs/procfs.h>
#include <fs/sysfs.h>
#include <fs/pseudofs.h>
#include <fs/fuse.h>
#include <fs/9p.h>

#include <sys/tests.h>

extern void ntsync_init(void);


#include <kern/console.h>
#include <kern/cmdline.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/version.h>
#include <kern/panic.h>
#include <kern/geom/geom.h>

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

typedef struct multiboot_module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} __attribute__((packed)) multiboot_module_t;

// 1. Address Translation Macros (since we are Higher Half)
#define PHYSICAL_d(x) ((uint32_t)(x) - KERN_BASE)
#define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + KERN_BASE))

static void init_memory(multiboot_info_t *mboot_info) {
    uint32_t mmap_addr = 0;
    uint32_t mmap_length = 0;
    uint32_t mem_lower = 0;
    uint32_t mem_upper = 0;

    // Dump Memory Map Early
    if (mboot_info && (mboot_info->flags & MULTIBOOT_INFO_MEM_MAP)) {
        pmm_dump_mmap((uintptr_t)VIRTUAL_d(mboot_info->mmap_addr), mboot_info->mmap_length);
    }

    if (mboot_info && (mboot_info->flags & MULTIBOOT_INFO_MEM_MAP)) {
        mmap_addr = (uintptr_t)VIRTUAL_d(mboot_info->mmap_addr);
        mmap_length = mboot_info->mmap_length;
        
        // Copy mmap for reclamation safety
        uint32_t count = mmap_length / sizeof(multiboot_mmap_entry_t);
        if (count > 64) count = 64;
        memcpy(mboot_mmap_copy, (void*)(uintptr_t)mmap_addr, count * sizeof(multiboot_mmap_entry_t));
        mboot_copy.mmap_addr = PHYSICAL_d(mboot_mmap_copy);
        mboot_copy.mmap_length = count * sizeof(multiboot_mmap_entry_t);
        mboot_copy.flags |= MULTIBOOT_INFO_MEM_MAP;
    }

    if (mboot_info && (mboot_info->flags & MULTIBOOT_INFO_MEMORY)) {
        mem_lower = mboot_info->mem_lower;
        mem_upper = mboot_info->mem_upper;
    }

    if (mboot_info) {
        pmm_record_boot_info(mboot_info);
    }

    // Initialize PMM
    if (mmap_addr) {
        pmm_init(mmap_addr, mmap_length, mem_lower, mem_upper);
        kprint("PMM Initialized with Multiboot mmap.\n");
    } else {
        pmm_init(0, 0, mem_lower, mem_upper);
        if (mem_upper || mem_lower) {
            kprint("PMM Initialized with legacy BIOS memory sizing.\n");
        } else {
            kprint("PMM Initialized (no mmap).\n");
        }
    }

    // Initialize VM subsystem
    extern void vm_object_init(void);
    extern void vm_zone_init(void);
    extern void uma_startup(void);
    extern void kmem_init(void);

    early_uart_print("KMAIN: vm_page_init\n");
    vm_page_init();
    early_uart_print("KMAIN: vm_object_init\n");
    vm_object_init();
    early_uart_print("KMAIN: vm_zone_init\n");
    vm_zone_init();
    // Discover Cores before UMA startup so UMA can init per-CPU caches
    early_uart_print("KMAIN: smp_discover_cores(2)\n");
    smp_discover_cores();
    early_uart_print("KMAIN: uma_startup\n");
    uma_startup();    // Initialize UMA before kmem (kmem uses UMA zones)
    early_uart_print("KMAIN: kmem_init\n");
    kmem_init();      // Initialize kernel memory allocator
    early_uart_print("KMAIN: vm ready\n");
    kprint("VM subsystem initialized.\n");
    
    // Update mboot_info global copy if needed
    if (mboot_info) {
        memcpy(&mboot_copy, mboot_info, sizeof(multiboot_info_t));
        if (mboot_info->flags & MULTIBOOT_INFO_MODS) {
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

static int parse_console_serial_index(const char *value) {
    if (!value) return -1;
    if (strncmp(value, "serial", 6) != 0) return -1;
    if (value[6] < '0' || value[6] > '3') return -1;
    if (value[7] != '\0') return -1;
    return value[6] - '0';
}

static int root_mount_with_type(const char *device, const char *fstype) {
    if (!device || !fstype || !*fstype) {
        return -1;
    }

    kprint("VFS: Trying root ");
    kprint(device);
    kprint(" as ");
    kprint(fstype);
    kprint("\n");

    return vfs_mount_legacy(device, "/", fstype, 0, NULL);
}

static int root_mount_auto(const char *device) {
    static const char *const root_fstypes[] = {
        "ext2",
        "fat",
        "minix",
        "udf",
        NULL
    };

    for (int i = 0; root_fstypes[i] != NULL; i++) {
        if (root_mount_with_type(device, root_fstypes[i]) == 0) {
            return 0;
        }
    }

    return -1;
}

static int root_mount_from_spec(const char *device, const char *spec) {
    char spec_buf[64];
    char *cursor;

    if (!device) {
        return -1;
    }

    if (!spec || !*spec || strcmp(spec, "auto") == 0) {
        return root_mount_auto(device);
    }

    strncpy(spec_buf, spec, sizeof(spec_buf) - 1);
    spec_buf[sizeof(spec_buf) - 1] = '\0';
    cursor = spec_buf;

    while (*cursor) {
        char *entry = cursor;

        while (*cursor && *cursor != ',') {
            cursor++;
        }
        if (*cursor == ',') {
            *cursor++ = '\0';
        }

        while (*entry == ' ' || *entry == '\t') {
            entry++;
        }

        char *end = entry + strlen(entry);
        while (end > entry && (end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }

        if (!*entry) {
            continue;
        }

        if (strcmp(entry, "auto") == 0) {
            if (root_mount_auto(device) == 0) {
                return 0;
            }
            continue;
        }

        if (root_mount_with_type(device, entry) == 0) {
            return 0;
        }
    }

    return -1;
}

static void register_boot_ramdisks(multiboot_info_t *mboot_info);
static void init_root_fs(void);

static multiboot_info_t *select_boot_info(unsigned long magic, unsigned long addr,
                                          char **cmdline_out) {
    multiboot_info_t *mboot_info = (multiboot_info_t *)addr;
    static multiboot_info_t fake_mbi;

    if (cmdline_out) {
        *cmdline_out = NULL;
    }

    if (magic == FREEBSD_LOADER_MAGIC) {
        memset(&fake_mbi, 0, sizeof(fake_mbi));
        mboot_info = &fake_mbi;
        kprint("Booted via FreeBSD loader.\n");
        return mboot_info;
    }

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        if (cmdline_out && (mboot_info->flags & MULTIBOOT_INFO_CMDLINE)) {
            *cmdline_out = (char *)VIRTUAL_d(mboot_info->cmdline);
        }
        return mboot_info;
    }

    kprint("Warning: Unknown bootloader magic, assuming raw boot.\n");
    return NULL;
}

static int init_cmdline_policy(const char *cmdline) {
    int cmdline_serial_console = -1;
    char console_param[32] = {0};

    if (cmdline) {
        cmdline_init(cmdline);
    } else {
        cmdline_init("");
    }
    kprint("\n");

    if (cmdline_get("console", console_param, sizeof(console_param)) != 0) {
        return -1;
    }

    if (strcmp(console_param, "console0") == 0) {
        kprint("console=console0: default console configuration.\n");
        return -1;
    }

    cmdline_serial_console = parse_console_serial_index(console_param);
    if (cmdline_serial_console < 0) {
        kprint("Unknown console= value: ");
        kprint(console_param);
        kprint(" (expected console0 or serial0..serial3)\n");
    }

    return cmdline_serial_console;
}

static void init_runtime_console(int serial_console) {
    int uart_ready = 0;

    console_init();
    hw_text_init();

    if (serial_console >= 0) {
        (void)uart_select_port((uint32_t)serial_console);
    }
    uart_ready = (uart_init() == 0);
    if (uart_ready && serial_console >= 0) {
        kprintf("console=serial%d selected (COM%d).\n",
                serial_console, serial_console + 1);
    }
    if (!uart_ready && (cmdline_has("serial_debug") || serial_console >= 0)) {
        kprint("Serial console requested but selected UART is not present.\n");
    }

    if (uart_ready && (cmdline_has("serial_debug") || serial_console >= 0)) {
        serial_debug_enabled = 1;
        console_register(uart_get_console());
        kprint("Serial Debug Enabled.\n");
    }

    if (cmdline_has("syscall_trace") || cmdline_has("syscall_log") ||
        cmdline_debug_enabled("syscall")) {
        syscall_trace_enabled = 1;
        kprint("Syscall Tracing Enabled.\n");
    }

}

static void init_core_subsystems(multiboot_info_t *mboot_info) {
    i386_cpu_init_early();
    percpu_init();

    gdt_init();
    kprint("GDT Initialized.\n");

    idt_init();
    timer_init();

    extern void fpu_init(void);
    fpu_init();

    rtc_init();
    pmap_bootstrap();

    smp_discover_cores();

    if (i386_cpu_has_apic()) {
        lapic_init();
        lapic_enable(0xFF);
    } else {
        kprint("SMP: Running in PIC/UP mode.\n");
    }

    extern void pmap_map_trampoline(void);
    pmap_map_trampoline();

    extern void random_init(void);
    random_init();
    exec_init();

    crc32_init();

    geom_init();
    geom_gpt_init();
    geom_mbr_init();
    geom_bsd_init();

    sched_init();
    kprint("Scheduler Initialized.\n");

    hw_text_late_init();

    if (i386_cpu_has_apic() && smp_get_cpu_count() > 1) {
        smp_boot_all_aps();
        sched_smp_init(smp_get_cpu_count());
    }

    extern void sysctl_init(void);
    sysctl_init();

    keyboard_init();
    if (mboot_info) {
        fb_init(mboot_info);
    }

}

static void init_storage_and_vfs(multiboot_info_t *mboot_info) {
    pci_init();
    isa_init();
    isa_probe_legacy();
    ide_init();
    virtio_init();
    register_boot_ramdisks(mboot_info);
    ntsync_init();

    run_kernel_tests();

    vfs_init();
    console_register_devfs();
    init_root_fs();

}

static void reclaim_bootloader_state(void) {
    multiboot_info_t *orig_mbi;
    uint32_t mods_addr = 0;
    int has_mods = 0;

    pmm_reclaim_setup();

    if (!mboot_orig_addr) {
        return;
    }

    orig_mbi = (multiboot_info_t *)mboot_orig_addr;
    if (orig_mbi->flags & MULTIBOOT_INFO_MODS) {
        has_mods = 1;
        mods_addr = orig_mbi->mods_addr;
    }

    kprint("Freeing Multiboot info: 4K\n");
    pmm_reclaim_range(mboot_orig_addr, mboot_orig_addr + 4096);

    if (has_mods) {
        kprint("Freeing Multiboot modules list: 4K\n");
        pmm_reclaim_range(mods_addr, mods_addr + 4096);
    }
}

static void print_boot_diagnostics(void) {
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
}

static void enter_kernel_idle_loop(void) __attribute__((noreturn));
static void enter_kernel_idle_loop(void) {
    kprint("Entering main loop...\n");
    while (1) {
        sched_yield();
        __asm__ volatile("sti; hlt");
    }
}

static void register_boot_ramdisks(multiboot_info_t *mboot_info) {
    if (!mboot_info) return;
    if (!(mboot_info->flags & MULTIBOOT_INFO_MODS)) return;
    if (!mboot_info->mods_addr || mboot_info->mods_count == 0) return;

    uint32_t mods_count = mboot_info->mods_count;
    if (mods_count > 8) {
        mods_count = 8;
    }

    multiboot_module_t *mods = (multiboot_module_t *)(uintptr_t)VIRTUAL_d(mboot_info->mods_addr);

    for (uint32_t i = 0; i < mods_count; i++) {
        uint32_t mod_start = mods[i].mod_start;
        uint32_t mod_end = mods[i].mod_end;
        if (mod_end <= mod_start) {
            continue;
        }

        size_t mod_size = (size_t)(mod_end - mod_start);
        void *mod_virt = VIRTUAL_d(mod_start);
        int ram_id = ramdisk_create(mod_virt, mod_size);

        if (ram_id >= 0) {
            kprintf("ramdisk: module %u -> /dev/storage/ram%d (%u bytes)\n",
                    i, ram_id, (unsigned)mod_size);
        } else {
            kprintf("ramdisk: failed to register module %u (0x%08x-0x%08x)\n",
                    i, mod_start, mod_end);
        }
    }
}





static void init_root_fs(void) {
    static const char *pseudo_mounts[] = { "/dev", "/proc", "/sys", NULL };
    const char *fallback_root = "/dev/storage/ram0";

    // Parse root= argument
    char root_dev[64] = {0};
    char root_type[64] = {0};
    int have_root_type = (cmdline_get("rootfstype", root_type, sizeof(root_type)) == 0);
    if (cmdline_get("root", root_dev, sizeof(root_dev)) == 0) {
        kprint("Mounting root from: ");
        kprint(root_dev);
        kprint("\n");

        if (root_mount_from_spec(root_dev, have_root_type ? root_type : NULL) != 0) {
            kprint("VFS: Cannot mount root ");
            kprint(root_dev);
            kprint("\n");

            kprint("VFS: Trying fallback root ");
            kprint(fallback_root);
            kprint("\n");
            if (root_mount_from_spec(fallback_root, have_root_type ? root_type : NULL) != 0) {
                panic("not syncing - cannot mount root!");
            }
        }
    } else {
        kprint("VFS: No root= argument specified.\n");
        kprint("VFS: Trying fallback root ");
        kprint(fallback_root);
        kprint("\n");
        if (root_mount_from_spec(fallback_root, have_root_type ? root_type : NULL) != 0) {
            panic("not syncing - cannot mount root!");
        }
    }

    if (!fs_root) {
        panic("not syncing - cannot mount root!");
    }

    // Ensure pseudo mountpoints exist and are directories.
    for (int i = 0; pseudo_mounts[i] != NULL; i++) {
        fs_node_t *mp = vfs_lookup(fs_root, pseudo_mounts[i]);
        if (!mp) {
            if (vfs_mkdir(pseudo_mounts[i], 0755) == 0) {
                kprint("VFS: Created mountpoint ");
                kprint(pseudo_mounts[i]);
                kprint("\n");
            } else {
                kprint("VFS: Failed to create mountpoint ");
                kprint(pseudo_mounts[i]);
                kprint("\n");
            }
            mp = vfs_lookup(fs_root, pseudo_mounts[i]);
        }

        if (!mp) {
            kprint("VFS: Mountpoint missing: ");
            kprint(pseudo_mounts[i]);
            kprint("\n");
        } else if ((mp->flags & 0x7) != FS_DIRECTORY) {
            kprint("VFS: Mountpoint not a directory: ");
            kprint(pseudo_mounts[i]);
            kprint("\n");
        }
    }

    // Mount pseudo-filesystems AFTER root is established.
    if (vfs_mount_legacy(NULL, "/dev", "devfs", 0, NULL) != 0) {
        kprint("VFS: Failed to mount devfs on /dev\n");
    } else {
        kprint("VFS: Mounted devfs on /dev\n");
    }
    if (vfs_mount_legacy(NULL, "/proc", "procfs", 0, NULL) != 0) {
        kprint("VFS: Failed to mount procfs on /proc\n");
    } else {
        kprint("VFS: Mounted procfs on /proc\n");
    }
    if (vfs_mount_legacy(NULL, "/sys", "sysfs", 0, NULL) != 0) {
        kprint("VFS: Failed to mount sysfs on /sys\n");
    } else {
        kprint("VFS: Mounted sysfs on /sys\n");
    }
}

// kinit - kernel init task (becomes PID 1 after exec)
// This is forked from kernel task 0 and execs the init binary
void kinit_task(void *arg) {
    (void)arg;  // Unused now that we use cmdline_get
    char *init_path = NULL;
    char *const init_envp[] = {
        "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
        "HOME=/",
        "TERM=linux",
        NULL
    };
    
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
        char *init_argv[] = { init_path, NULL };
        if (kern_execve(init_path, init_argv, init_envp) == 0) {
            goto exec_success;
        }
        panic("kinit: Requested init failed.");
    }

    // Default paths
    kprint("kinit: Trying default init paths...\n");
    const char *init_paths[] = {
        "/sbin/init",
        "/etc/init",
        "/bin/init",
        "/bin/sh",
        NULL
    };
    for (int i = 0; init_paths[i] != NULL; i++) {
        char *default_argv[] = { (char *)init_paths[i], NULL };
        if (kern_execve(init_paths[i], default_argv, init_envp) == 0) {
            goto exec_success;
        }
    }

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

    early_uart_print("KMAIN: pm_init\n");
    pm_init();
    current_process = &processes[0];
    current_process->pid = 0;
    strcpy(current_process->comm, "(swapper)");

    // boot.S converts the Multiboot info pointer to higher-half virtual, but
    // pointers inside the structure remain physical until translated here.
    multiboot_info_t *mboot_info;
    char *cmdline = NULL;
    int cmdline_serial_console;

    mboot_info = select_boot_info(magic, addr, &cmdline);
    cmdline_serial_console = init_cmdline_policy(cmdline);

    smp_init();
    init_memory(mboot_info);
    init_runtime_console(cmdline_serial_console);

    kprint(OS_NAME " kernel v" OS_VERSION " (i386)\n");
    init_core_subsystems(mboot_info);
    print_boot_diagnostics();
    init_storage_and_vfs(mboot_info);
    sched_spawn_kernel_process(init_task, cmdline);
    vm_page_late_init();
    reclaim_bootloader_state();
    enter_kernel_idle_loop();
}
