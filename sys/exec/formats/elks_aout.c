#include <exec/formats/elks_aout.h>
#include <sys/exec.h>
#include <sys/errno.h>
#include <string.h>
#include <kern/console.h>
#include <exec/perso/personality.h>
#include <sys/proc.h>
#include <sys/ldt.h>
#include <vm/vm_map.h>
#include <vm/vm_kmem.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <vfs/vfs.h>
#include <sys/kern_syscalls.h>
#include <sys/file.h>
#include <sys/sysctl.h> // For HW_PHYSMEM in future?
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <kern/panic.h>

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
    (void)path;
    return elks_header_recognized(header, len) ? 0 : -ENOEXEC;
}

static int elks_setup_segments(process_t *proc, const struct elks_load_plan *plan) {
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
    cs_desc.base_addr = plan->text_base;
    cs_desc.limit = plan->text_limit ? (uint32_t)(plan->text_limit - 1) : 0;
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
    ds_desc.base_addr = plan->data_base;
    ds_desc.limit = plan->data_limit ? (uint32_t)(plan->data_limit - 1) : 0;
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
    struct elks_supl_hdr suph;
    struct elks_load_plan plan;
    (void)argv;
    (void)envp;
    
    if (kern_read(fd, (char *)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        kern_close(fd);
        return -EIO;
    }
    memset(&suph, 0, sizeof(suph));
    memset(&plan, 0, sizeof(plan));
    if (!elks_header_recognized(&hdr, sizeof(hdr))) {
        kern_close(fd);
        return -ENOEXEC;
    }
    if (hdr.hlen > sizeof(hdr)) {
        size_t extra = (size_t)hdr.hlen - sizeof(hdr);

        if (extra > sizeof(suph)) {
            kern_close(fd);
            return -ENOEXEC;
        }
        if (kern_read(fd, (char *)&suph, extra) != (int)extra) {
            kern_close(fd);
            return -EIO;
        }
    }
    if (!elks_build_load_plan(&hdr, &suph, 0, &plan)) {
        kern_close(fd);
        return -ENOEXEC;
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
    
    // Allocate and map memory for Text
    for (uint32_t va = plan.text_base;
         va < plan.text_base + ((plan.text_limit + 0x0FFFU) & ~0x0FFFU);
         va += 0x1000) {
        void *pa = pmm_alloc_block();
        if (!pa) return -ENOMEM;
        uint32_t pa_phys = (uint32_t)pa - 0xC0000000;
        pmap_enter(pmap, va, pa_phys, VM_PROT_READ | VM_PROT_EXEC | VM_PROT_WRITE, 0);
        memset(pa, 0, 0x1000);
    }
    
    if (!plan.combined) {
        for (uint32_t va = plan.data_base;
             va < plan.data_base + ((plan.data_limit + 0x0FFFU) & ~0x0FFFU);
             va += 0x1000) {
            void *pa = pmm_alloc_block();
            if (!pa) return -ENOMEM;
            uint32_t pa_phys = (uint32_t)pa - 0xC0000000;
            pmap_enter(pmap, va, pa_phys, VM_PROT_READ | VM_PROT_WRITE, 0);
            memset(pa, 0, 0x1000);
        }
    }

    if (plan.fartext_size > 0) {
        for (uint32_t va = plan.fartext_base;
             va < plan.fartext_base + ((plan.fartext_size + 0x0FFFU) & ~0x0FFFU);
             va += 0x1000) {
            void *pa = pmm_alloc_block();
            if (!pa) return -ENOMEM;
            uint32_t pa_phys = (uint32_t)pa - 0xC0000000;
            pmap_enter(pmap, va, pa_phys, VM_PROT_READ | VM_PROT_EXEC | VM_PROT_WRITE, 0);
            memset(pa, 0, 0x1000);
        }
    }
    
    // Read Text segment
    if (plan.text_size > 0) {
        kern_lseek(fd, (off_t)plan.text_file_offset, 0);
        if (kern_read(fd, (void *)plan.text_base, plan.text_size) != (int)plan.text_size) {
            return -EIO;
        }
    }

    if (plan.fartext_size > 0) {
        kern_lseek(fd, (off_t)plan.fartext_file_offset, 0);
        if (kern_read(fd, (void *)plan.fartext_base, plan.fartext_size) != (int)plan.fartext_size) {
            return -EIO;
        }
    }

    if (plan.data_size > 0) {
        uint32_t data_load_base = plan.combined ? (plan.text_base + plan.text_size) : plan.data_base;

        kern_lseek(fd, (off_t)plan.data_file_offset, 0);
        if (kern_read(fd, (void *)data_load_base, plan.data_size) != (int)plan.data_size) {
            return -EIO;
        }
        memset((void *)(uintptr_t)(data_load_base + plan.data_size), 0, plan.bss_size);
    }
    
    // Setup LDT
    if (elks_setup_segments(current_process, &plan) != 0) {
        return -ENOMEM;
    }
    ldt_activate(current_process);
    
    // Set Personality and Bitness
    current_process->perso_id = PERS_ELKS;
    current_process->bitness = BITNESS_16;
    
    current_process->brk_start = plan.data_base + plan.brk_offset;
    current_process->brk = current_process->brk_start;
    
    // Set up process name
    const char *name = path;
    for (const char *p = path; *p; p++) if (*p == '/') name = p + 1;
    strncpy(current_process->comm, name, sizeof(current_process->comm) - 1);
    
    // Stack setup (simplified for now)
    // 16-bit stack points to end of Data segment
    uint32_t user_sp = plan.stack_top ? plan.stack_top : 0xFFFE;
    
    kprint("ELKS: Loaded binary, jumping to 16-bit mode\n");
    
    extern void jump_to_elks(uint32_t entry, uint32_t stack, uint32_t cs, uint32_t ds);
    
    // Selectors: index << 3 | LDT=4 | RPL=3
    uint32_t cs_sel = (0 << 3) | 4 | 3;
    uint32_t ds_sel = (1 << 3) | 4 | 3;
    
    kern_close(fd);
    
    jump_to_elks(hdr.entry, user_sp, cs_sel, ds_sel);
    
    panic("jump_to_elks returned!");
    return 0;
}
