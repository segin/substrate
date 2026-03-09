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
#include <kern/arch.h>
#include <vm/vm_map.h>

static struct exec_binary_handler elks_handler = {
    .name = "ELKS a.out",
    .check = elks_check_file,
    .load = elks_load,
    .next = NULL
};

static int elks_fail(int fd, int err, const char *msg) {
    if (msg) {
        kprint(msg);
        kprint("\n");
    }
    if (fd >= 0) {
        kern_close(fd);
    }
    return err;
}

void elks_init_handler(void) {
    exec_register_handler(&elks_handler);
}

int elks_check_file(const char *path, const char *header, size_t len) {
    (void)path;
    return elks_header_recognized(header, len) ? 0 : -ENOEXEC;
}

static int elks_setup_segments(process_t *proc, const struct elks_load_plan *plan) {
    struct elks_segment_layout layout;

    if (ldt_alloc_process(proc, LDT_ENTRIES) != 0) {
        return -ENOMEM;
    }
    memset(&layout, 0, sizeof(layout));
    elks_build_segment_layout(plan, &layout);

    fill_ldt_entry((uint8_t *)proc->ldt + ELKS_LDT_CS_INDEX * 8, &layout.cs);
    fill_ldt_entry((uint8_t *)proc->ldt + ELKS_LDT_DS_INDEX * 8, &layout.ds);
    fill_ldt_entry((uint8_t *)proc->ldt + ELKS_LDT_SS_INDEX * 8, &layout.ss);
    fill_ldt_entry((uint8_t *)proc->ldt + ELKS_LDT_ES_INDEX * 8, &layout.es);
    
    return 0;
}

int elks_load(int fd, const char *path, char *const argv[], char *const envp[]) {
    struct elks_exec hdr;
    struct elks_supl_hdr suph;
    struct elks_load_plan plan;
    size_t argv_envp_bytes;
    
    kprint("ELKS: loading ");
    kprint(path ? path : "(null)");
    kprint("\n");

    if (fd >= 0 && fd < MAX_FD && current_process && current_process->fds[fd]) {
        current_process->fds[fd]->f_offset = 0;
    } else {
        kern_lseek(fd, 0, 0);
    }
    if (kern_read(fd, (char *)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        return elks_fail(fd, -EIO, "ELKS: failed to read executable header");
    }
    memset(&suph, 0, sizeof(suph));
    memset(&plan, 0, sizeof(plan));
    argv_envp_bytes = elks_stack_image_bytes(argv, envp);
    if (argv_envp_bytes > 0xFFFFU) {
        return elks_fail(fd, -E2BIG, "ELKS: argv/envp image exceeds 16-bit segment");
    }
    if (!elks_header_recognized(&hdr, sizeof(hdr))) {
        return elks_fail(fd, -ENOEXEC, "ELKS: header not recognized");
    }
    if (hdr.hlen > sizeof(hdr)) {
        size_t extra = (size_t)hdr.hlen - sizeof(hdr);

        if (extra > sizeof(suph)) {
            return elks_fail(fd, -ENOEXEC, "ELKS: supplemental header too large");
        }
        if (kern_read(fd, (char *)&suph, extra) != (int)extra) {
            return elks_fail(fd, -EIO, "ELKS: failed to read supplemental header");
        }
    }
    if (!elks_build_load_plan(&hdr, &suph, (uint16_t)argv_envp_bytes, &plan)) {
        return elks_fail(fd, -ENOEXEC, "ELKS: invalid load plan");
    }
    
    // Create new address space
    extern pmap_t pmap_create(void);
    pmap_t pmap = pmap_create();
    if (!pmap) {
        return elks_fail(fd, -ENOMEM, "ELKS: failed to create pmap");
    }
    
    current_process->pmap = (struct pmap*)pmap;
    extern void pmap_activate(pmap_t pmap);
    pmap_activate(pmap);
    
    // Allocate and map memory for Text
    for (uint32_t va = plan.text_base;
         va < plan.text_base + ((plan.text_limit + 0x0FFFU) & ~0x0FFFU);
         va += 0x1000) {
        void *pa = pmm_alloc_block();
        if (!pa) {
            return elks_fail(fd, -ENOMEM, "ELKS: failed to allocate text page");
        }
        uint32_t pa_phys = (uint32_t)pa - 0xC0000000;
        pmap_enter(pmap, va, pa_phys, VM_PROT_READ | VM_PROT_EXEC | VM_PROT_WRITE, 0);
        memset(pa, 0, 0x1000);
    }
    
    if (!plan.combined) {
        for (uint32_t va = plan.data_base;
             va < plan.data_base + ((plan.data_limit + 0x0FFFU) & ~0x0FFFU);
             va += 0x1000) {
            void *pa = pmm_alloc_block();
            if (!pa) {
                return elks_fail(fd, -ENOMEM, "ELKS: failed to allocate data page");
            }
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
            if (!pa) {
                return elks_fail(fd, -ENOMEM, "ELKS: failed to allocate far text page");
            }
            uint32_t pa_phys = (uint32_t)pa - 0xC0000000;
            pmap_enter(pmap, va, pa_phys, VM_PROT_READ | VM_PROT_EXEC | VM_PROT_WRITE, 0);
            memset(pa, 0, 0x1000);
        }
    }
    
    // Read Text segment
    if (plan.text_size > 0) {
        kern_lseek(fd, (off_t)plan.text_file_offset, 0);
        if (kern_read(fd, (void *)plan.text_base, plan.text_size) != (int)plan.text_size) {
            return elks_fail(fd, -EIO, "ELKS: failed to read text segment");
        }
    }

    if (plan.fartext_size > 0) {
        kern_lseek(fd, (off_t)plan.fartext_file_offset, 0);
        if (kern_read(fd, (void *)plan.fartext_base, plan.fartext_size) != (int)plan.fartext_size) {
            return elks_fail(fd, -EIO, "ELKS: failed to read far text segment");
        }
    }

    if (plan.data_size > 0) {
        uint32_t data_load_base = plan.combined ? (plan.text_base + plan.text_size) : plan.data_base;

        kern_lseek(fd, (off_t)plan.data_file_offset, 0);
        if (kern_read(fd, (void *)data_load_base, plan.data_size) != (int)plan.data_size) {
            return elks_fail(fd, -EIO, "ELKS: failed to read data segment");
        }
        memset((void *)(uintptr_t)(data_load_base + plan.data_size), 0, plan.bss_size);
    }
    
    // Setup LDT
    if (elks_setup_segments(current_process, &plan) != 0) {
        return elks_fail(fd, -ENOMEM, "ELKS: failed to allocate or populate LDT");
    }
    ldt_activate(current_process);
    
    elks_apply_exec_state(current_process, &plan, path);
    if (current_process->vm_map) {
        vm_map_destroy(current_process->vm_map);
    }
    current_process->vm_map = vm_map_create(pmap, 0, 0xC0000000U);
    arch_set_kernel_stack((uintptr_t)current_thread->kstack_top);
    
    // Stack setup (simplified for now)
    // 16-bit stack points to end of Data segment
    uint16_t initial_sp = 0;
    uint32_t user_sp;
    struct elks_segment_layout layout;
    
    kprint("ELKS: Loaded binary, jumping to 16-bit mode\n");
    
    memset(&layout, 0, sizeof(layout));
    elks_build_segment_layout(&plan, &layout);
    if (!elks_build_stack_image((uint8_t *)(uintptr_t)plan.data_base, &plan,
                                argv, envp, &initial_sp)) {
        return elks_fail(fd, -E2BIG, "ELKS: failed to build startup stack image");
    }
    user_sp = initial_sp;

    extern void jump_to_elks(uint32_t entry, uint32_t stack, uint32_t cs,
                             uint32_t ds, uint32_t ss, uint32_t es);
    
    kern_close(fd);
    
    jump_to_elks(hdr.entry, user_sp ? user_sp : 0xFFFE, layout.cs_sel, layout.ds_sel,
                 layout.ss_sel, layout.es_sel);
    
    panic("jump_to_elks returned!");
    return 0;
}
