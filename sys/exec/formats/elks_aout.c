#include <exec/formats/elks_aout.h>
#include <sys/exec.h>
#include <sys/errno.h>
#include <string.h>
#include <kern/console.h>
#include <exec/perso/personality.h>
#include <sys/proc.h>
#include <sys/ldt.h>
#include <vm/vm_map.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <vfs/vfs.h>
#include <lib/lib.h>
#include <sys/kern_syscalls.h>
#include <sys/file.h>
#include <sys/sysctl.h> // For HW_PHYSMEM in future?
#include <sys/stat.h>
#include <sys/fcntl.h>

#define ELKS_TEXT_BASE 0x10000
#define ELKS_DATA_BASE 0x20000

static struct exec_binary_handler elks_handler = {
    .name = "ELKS a.out",
    .check = elks_check_file,
    .load = elks_load,
    .next = NULL
};

void elks_init_handler(void) {
    exec_register_handler(&elks_handler);
}

int elks_check_file(const char *path, const char *header, size_t len) {
    if (len < sizeof(struct elks_exec)) return -ENOEXEC;
    
    struct elks_exec *hdr = (struct elks_exec *)header;
    
    // Check magic (0x01 0x03 for 0x0301 little-endian)
    if (hdr->a_magic[0] != ELKS_MAG0 || hdr->a_magic[1] != ELKS_MAG1) {
        return -ENOEXEC;
    }
    
    // Check CPU type
    if (hdr->a_cpu != ELKS_CPU_8086 && hdr->a_cpu != ELKS_CPU_80286) {
        return -ENOEXEC;
    }
    
    return 0; // Match
}

static int elks_setup_segments(process_t *proc, struct elks_exec *hdr) {
    /* Allocate LDT if needed */
    if (!proc->ldt) {
        proc->ldt = kmalloc(LDT_ENTRIES * 8);
        if (!proc->ldt) return -ENOMEM;
        memset(proc->ldt, 0, LDT_ENTRIES * 8);
        proc->ldt_entry_count = LDT_ENTRIES;
    }
    
    // Selector 0: CS
    struct user_desc cs_desc;
    memset(&cs_desc, 0, sizeof(cs_desc));
    cs_desc.entry_number = 0;
    cs_desc.base_addr = ELKS_TEXT_BASE;
    cs_desc.limit = 0xFFFF; // 64KB limit
    cs_desc.seg_32bit = 0;   // 16-bit
    cs_desc.contents = 2;    // Code
    cs_desc.read_exec_only = 0;
    cs_desc.limit_in_pages = 0;
    cs_desc.seg_not_present = 0;
    cs_desc.useable = 1;
    
    // Selector 1: DS/SS
    struct user_desc ds_desc;
    memset(&ds_desc, 0, sizeof(ds_desc));
    ds_desc.entry_number = 1;
    ds_desc.base_addr = ELKS_DATA_BASE;
    ds_desc.limit = 0xFFFF; // 64KB limit
    ds_desc.seg_32bit = 0;   // 16-bit
    ds_desc.contents = 0;    // Data
    ds_desc.read_exec_only = 0;
    ds_desc.limit_in_pages = 0;
    ds_desc.seg_not_present = 0;
    ds_desc.useable = 1;
    
    fill_ldt_entry((uint8_t*)proc->ldt + 0*8, &cs_desc);
    fill_ldt_entry((uint8_t*)proc->ldt + 1*8, &ds_desc);
    
    return 0;
}

int elks_load(int fd, const char *path, char *const argv[], char *const envp[]) {
    struct elks_exec hdr;
    
    if (kern_read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        kern_close(fd);
        return -EIO;
    }
    
    // Create new address space
    extern pmap_t pmap_create(void);
    pmap_t pmap = pmap_create();
    if (!pmap) {
        kern_close(fd);
        return -ENOMEM;
    }
    
    current_process->pmap = (struct pmap*)pmap;
    extern void pmap_activate(pmap_t pmap);
    pmap_activate(pmap);
    
    // Allocate and map memory for Text (64KB at ELKS_TEXT_BASE)
    for (uint32_t va = ELKS_TEXT_BASE; va < ELKS_TEXT_BASE + 0x10000; va += 0x1000) {
        void *pa = pmm_alloc_block();
        if (!pa) return -ENOMEM;
        uint32_t pa_phys = (uint32_t)pa - 0xC0000000;
        pmap_enter(pmap, va, pa_phys, VM_PROT_READ | VM_PROT_EXEC | VM_PROT_WRITE, 0);
        memset(pa, 0, 0x1000);
    }
    
    // Allocate and map memory for Data (64KB at ELKS_DATA_BASE)
    for (uint32_t va = ELKS_DATA_BASE; va < ELKS_DATA_BASE + 0x10000; va += 0x1000) {
        void *pa = pmm_alloc_block();
        if (!pa) return -ENOMEM;
        uint32_t pa_phys = (uint32_t)pa - 0xC0000000;
        pmap_enter(pmap, va, pa_phys, VM_PROT_READ | VM_PROT_WRITE, 0);
        memset(pa, 0, 0x1000);
    }
    
    // Read Text segment
    if (hdr.a_text > 0) {
        kern_lseek(fd, hdr.a_hdrlen, 0); // SEEK_SET
        if (kern_read(fd, (void*)ELKS_TEXT_BASE, hdr.a_text) != (int)hdr.a_text) {
            return -EIO;
        }
    }
    
    // Read Data segment
    if (hdr.a_data > 0) {
        kern_lseek(fd, hdr.a_hdrlen + hdr.a_text, 0);
        if (kern_read(fd, (void*)ELKS_DATA_BASE, hdr.a_data) != (int)hdr.a_data) {
            return -EIO;
        }
    }
    
    // BSS is already zeroed
    
    // Setup LDT
    if (elks_setup_segments(current_process, &hdr) != 0) {
        return -ENOMEM;
    }
    ldt_activate(current_process);
    
    // Set Personality and Bitness
    current_process->perso_id = PERS_ELKS;
    current_process->bitness = BITNESS_16;
    
    // Set up process name
    const char *name = path;
    for (const char *p = path; *p; p++) if (*p == '/') name = p + 1;
    strncpy(current_process->comm, name, sizeof(current_process->comm) - 1);
    
    // Stack setup (simplified for now)
    // 16-bit stack points to end of Data segment
    uint32_t user_sp = 0xFFFE; 
    
    kprint("ELKS: Loaded binary, jumping to 16-bit mode\n");
    
    extern void jump_to_elks(uint32_t entry, uint32_t stack, uint32_t cs, uint32_t ds);
    
    // Selectors: index << 3 | LDT=4 | RPL=3
    uint32_t cs_sel = (0 << 3) | 4 | 3;
    uint32_t ds_sel = (1 << 3) | 4 | 3;
    
    kern_close(fd);
    
    jump_to_elks(hdr.a_entry, user_sp, cs_sel, ds_sel);
    
    panic("jump_to_elks returned!");
    return 0;
}
