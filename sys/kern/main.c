#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/proc.h>
#include <sys/input.h>

#include <drivers/video/vga.h>
#include <drivers/video/fb.h>
#include <drivers/video/hw_text.h>
#include <drivers/serial/uart.h>
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
#include <vfs/vfs.h>
#include <exec/formats/elf.h>
#include <fs/procfs.h>
#include <fs/sysfs.h>
#include <fs/pseudofs.h>
#include <fs/fuse.h>
#include <fs/9p.h>

#include <tests/tests.h>

#include <kern/console.h>
#include <kern/cmdline.h>
#include <kern/sched.h>
#include <kern/version.h>
#include <kern/panic.h>

// Simple string functions to avoid depending on libc in core if not available
int serial_debug_enabled = 0;
int syscall_trace_enabled = 0;
char kernel_hostname[65] = "localhost";


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

static void early_uart_putc(char c) {
    uint16_t port = 0x3F8;
    // Wait for transmit buffer empty
    uint8_t status;
    do {
        __asm__ volatile("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)(port + 5)));
    } while ((status & 0x20) == 0);
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)c), "Nd"(port));
}

static void early_uart_print(const char *s) {
    while (*s) early_uart_putc(*s++);
}

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

/* Early GDT for early exceptions */
struct early_gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct early_gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct early_gdt_entry early_gdt[3];
static struct early_gdt_ptr early_gdt_ptr;

/* Early IDT for debugging boot faults */
struct early_idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct early_idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct early_idt_entry early_idt[32];
static struct early_idt_ptr early_idt_ptr;

/* Current exception number - set by stubs */
static volatile int early_exception_num = -1;

/* Hex digit lookup */
static const char hex_digits[] = "0123456789ABCDEF";

static void early_print_hex(uint32_t val) {
    char buf[11] = "0x00000000";
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex_digits[val & 0xF];
        val >>= 4;
    }
    early_uart_print(buf);
}

/* Early exception handler - prints via UART and halts */
void early_exception_handler(void) {
    early_uart_print("!!! EARLY EXCEPTION #");
    early_print_hex(early_exception_num);
    early_uart_print(" !!!\n");
    
    /* Try to get some useful info from stack */
    uint32_t eip;
    __asm__ volatile("mov 4(%%ebp), %0" : "=r"(eip));
    early_uart_print("EIP: ");
    early_print_hex(eip);
    early_uart_print("\n");
    
    /* Halt forever */
    for (;;) __asm__ volatile("hlt");
}

/* Macro to generate exception stubs */
#define EARLY_ISR(n) \
    __attribute__((naked)) void early_isr##n(void) { \
        __asm__ volatile( \
            "pusha\n" \
            "movl $" #n ", %0\n" \
            "call early_exception_handler\n" \
            "popa\n" \
            "iret\n" \
            : "=m"(early_exception_num) \
        ); \
    }

EARLY_ISR(0)  EARLY_ISR(1)  EARLY_ISR(2)  EARLY_ISR(3)
EARLY_ISR(4)  EARLY_ISR(5)  EARLY_ISR(6)  EARLY_ISR(7)
EARLY_ISR(8)  EARLY_ISR(9)  EARLY_ISR(10) EARLY_ISR(11)
EARLY_ISR(12) EARLY_ISR(13) EARLY_ISR(14) EARLY_ISR(15)
EARLY_ISR(16) EARLY_ISR(17) EARLY_ISR(18) EARLY_ISR(19)
EARLY_ISR(20) EARLY_ISR(21) EARLY_ISR(22) EARLY_ISR(23)
EARLY_ISR(24) EARLY_ISR(25) EARLY_ISR(26) EARLY_ISR(27)
EARLY_ISR(28) EARLY_ISR(29) EARLY_ISR(30) EARLY_ISR(31)

/* Array of handler pointers */
static void (*early_isr_table[32])(void) = {
    early_isr0,  early_isr1,  early_isr2,  early_isr3,
    early_isr4,  early_isr5,  early_isr6,  early_isr7,
    early_isr8,  early_isr9,  early_isr10, early_isr11,
    early_isr12, early_isr13, early_isr14, early_isr15,
    early_isr16, early_isr17, early_isr18, early_isr19,
    early_isr20, early_isr21, early_isr22, early_isr23,
    early_isr24, early_isr25, early_isr26, early_isr27,
    early_isr28, early_isr29, early_isr30, early_isr31
};

static void early_gdt_init(void) {
    /* Entry 0: Null descriptor */
    early_gdt[0].limit_low = 0;
    early_gdt[0].base_low = 0;
    early_gdt[0].base_mid = 0;
    early_gdt[0].access = 0;
    early_gdt[0].granularity = 0;
    early_gdt[0].base_high = 0;
    
    /* Entry 1 (0x08): Code segment - base 0, limit 4GB, execute/read */
    early_gdt[1].limit_low = 0xFFFF;
    early_gdt[1].base_low = 0;
    early_gdt[1].base_mid = 0;
    early_gdt[1].access = 0x9A;      /* Present, ring 0, code, execute/read */
    early_gdt[1].granularity = 0xCF; /* 4KB granularity, 32-bit */
    early_gdt[1].base_high = 0;
    
    /* Entry 2 (0x10): Data segment - base 0, limit 4GB, read/write */
    early_gdt[2].limit_low = 0xFFFF;
    early_gdt[2].base_low = 0;
    early_gdt[2].base_mid = 0;
    early_gdt[2].access = 0x92;      /* Present, ring 0, data, read/write */
    early_gdt[2].granularity = 0xCF; /* 4KB granularity, 32-bit */
    early_gdt[2].base_high = 0;
    
    early_gdt_ptr.limit = sizeof(early_gdt) - 1;
    early_gdt_ptr.base = (uint32_t)&early_gdt;
    
    /* Load GDT and reload segment registers */
    __asm__ volatile(
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "m"(early_gdt_ptr) : "eax"
    );
}

static void early_idt_set_gate(int n, uint32_t handler) {
    early_idt[n].offset_low = handler & 0xFFFF;
    early_idt[n].selector = 0x08;  /* Kernel code segment */
    early_idt[n].zero = 0;
    early_idt[n].type_attr = 0x8E; /* Present, ring 0, 32-bit interrupt gate */
    early_idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

static void early_idt_init(void) {
    early_idt_ptr.limit = sizeof(early_idt) - 1;
    early_idt_ptr.base = (uint32_t)&early_idt;
    
    /* Set all 32 exception vectors to our early handlers */
    for (int i = 0; i < 32; i++) {
        early_idt_set_gate(i, (uint32_t)early_isr_table[i]);
    }
    
    /* Load the IDT */
    __asm__ volatile("lidt %0" : : "m"(early_idt_ptr));
}

// Kernel Entry Point
void kmain(unsigned long magic, unsigned long addr) {
    /* Initialize early GDT and IDT FIRST to catch any faults */
    early_gdt_init();
    early_idt_init();
    
    early_uart_print("KMAIN START\n");
    mboot_orig_addr = addr;
    // 0. Setup Kernel Task IMMEDIATELY
    pm_init();
    current_process = &processes[0];
    current_process->pid = 0;
    strcpy(current_process->comm, "(swapper)");

    console_init();
    hw_text_init(); // Registers VGA text console
    uart_init(); // Initializes UART hardare

    // 1. Address Translation Macros (since we are Higher Half)
    #define PHYSICAL_d(x) ((uint32_t)(x) - 0xC0000000)
    #define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + 0xC0000000))

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


    // Check for serial debug
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

    // Dump Memory Map Early
    if (mboot_info && (mboot_info->flags & (1<<6))) {
        pmm_dump_mmap((uintptr_t)VIRTUAL_d(mboot_info->mmap_addr), mboot_info->mmap_length);
    }

    uint32_t mmap_addr = 0;
    uint32_t mmap_length = 0;
    
    // Parse Multiboot Modules (Initrd)
    if (mboot_info && (mboot_info->flags & (1<<3))) {
        uint32_t mods_count = mboot_info->mods_count;
        uint32_t mods_addr_phys = mboot_info->mods_addr;
        uint32_t mods_addr_virt = (uintptr_t)VIRTUAL_d(mods_addr_phys);
        
        kprint("Multiboot Modules found: ");
        char c = mods_count + '0'; 
        char s[2] = {c, 0};
        kprint(s);
        kprint("\n");
        
        if (mods_count > 0) {
            struct multiboot_mod_list {
                uint32_t mod_start;
                uint32_t mod_end;
                uint32_t cmdline;
                uint32_t pad;
            } *mod = (struct multiboot_mod_list *)(uintptr_t)mods_addr_virt;
            
            ramdisk_init(VIRTUAL_d(mod->mod_start), mod->mod_end - mod->mod_start);
        }
    }
    
    if (mboot_info && (mboot_info->flags & (1<<6))) {
        mmap_addr = (uintptr_t)VIRTUAL_d(mboot_info->mmap_addr);
        mmap_length = mboot_info->mmap_length;
        
        // Copy mmap
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
    uma_startup();    // Initialize UMA before kmem (kmem uses UMA zones)
    kmem_init();      // Initialize kernel memory allocator
    kprint("VM subsystem initialized.\n");

    // Update mboot_info to point to our copies
    if (mboot_info) {
        memcpy(&mboot_copy, mboot_info, sizeof(multiboot_info_t));
        // mmap updated above if present
        
        // Modules updated if present
        if (mboot_info->flags & (1<<3)) {
             uint32_t mods_count = mboot_info->mods_count;
             if (mods_count > 8) mods_count = 8;
             uint32_t mods_addr_virt = (uintptr_t)VIRTUAL_d(mboot_info->mods_addr);
             memcpy(mboot_mods_copy, (void*)(uintptr_t)mods_addr_virt, mods_count * 16);
             mboot_copy.mods_addr = PHYSICAL_d(mboot_mods_copy);
             mboot_copy.mods_count = mods_count;
        }
        
        mboot_info = &mboot_copy;
    }
    
    if (mboot_info && (mboot_info->flags & (1<<6))) {
        mmap_addr = (uintptr_t)VIRTUAL_d(mboot_info->mmap_addr);
        mmap_length = mboot_info->mmap_length;
        
        // Copy mmap
        uint32_t count = mmap_length / sizeof(multiboot_mmap_entry_t);
        if (count > 64) count = 64;
        memcpy(mboot_mmap_copy, (void*)(uintptr_t)mmap_addr, count * sizeof(multiboot_mmap_entry_t));
        mboot_copy.mmap_addr = PHYSICAL_d(mboot_mmap_copy);
        mboot_copy.mmap_length = count * sizeof(multiboot_mmap_entry_t);
        mboot_copy.flags |= (1<<6);
    }

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

    // Initialize Scheduler
    sched_init();
    kprint("Scheduler Initialized.\n");
    
    keyboard_init();
    
    // Initialize Framebuffer Console (if available) - Defer until paging is up
    if (mboot_info) {
        fb_init(mboot_info);
    }
    
    // Display kernel ident banner (first thing user sees)
    kprint(OS_NAME " kernel v" OS_VERSION " (i386)\n");

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
    
    // Run Kernel Tests (if requested via cmdline 'test=...')
    run_kernel_tests();
    
    // Initialize VFS (handles filesystem registration and pseudo-fs mounts)
    vfs_init();
    
    // Register console device in /dev
    console_register_devfs();
    
    // Check for Root Filesystem
    // Parse root= argument
    char root_dev[64] = {0};
    if (cmdline_get("root", root_dev, sizeof(root_dev)) == 0) {
        kprint("Mounting root from: ");
        kprint(root_dev);
        kprint("\n");
        
        // TODO: Wait for device to appear if needed?
        // Simple loop?
        
        // Try to mount as specific types or auto-detect if we had a list.
        // For now, let's look for rootfstype=
        char root_type[32] = {0};
        if (cmdline_get("rootfstype", root_type, sizeof(root_type)) != 0) {
             // Default to ext2 if not specified, or try multiple?
             strcpy(root_type, "ext2");
        }
        
        if (vfs_mount(root_dev, "/", root_type, 0, NULL) != 0) {
            kprint("VFS: Cannot mount root ");
            kprint(root_dev);
            kprint("\n");
            
            // Try fallback to initrd if it was specified
            kprint("VFS: Trying /dev/storage/ram0\n");
            if (vfs_mount("/dev/storage/ram0", "/", "ext2", 0, NULL) != 0) {
                 // Final Failure
                 panic("not syncing - cannot mount root!");
            }
        }
    } else {
        kprint("VFS: No root= argument specified.\n");
        kprint("VFS: Trying /dev/storage/ram0\n");
        // Fallback to initrd
        if (vfs_mount("/dev/storage/ram0", "/", "ext2", 0, NULL) != 0) {
            panic("not syncing - cannot mount root!");
        }
    }

    if (!fs_root) {
        panic("not syncing - cannot mount root!");
    }

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
