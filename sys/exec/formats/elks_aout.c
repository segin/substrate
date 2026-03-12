#include <exec/formats/elks_aout.h>
#include <sys/exec.h>
#include <sys/errno.h>
#include <string.h>
#include <kern/console.h>
#include <exec/perso/personality.h>
#include <sys/compiler.h>
#include <sys/proc.h>
#include <sys/ldt.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
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
#include <sys/copy.h>
#include <pm/pm.h>
#include <kern/panic.h>
#include <kern/arch.h>
#include <kern/cmdline.h>

#define ELKS_ARG_MAX_BYTES (32 * 1024)
#define ELKS_ARG_MAX_COUNT 4096

static struct exec_binary_handler elks_handler = {
    .name = "ELKS a.out",
    .check = elks_check_file,
    .load = elks_load,
    .next = NULL
};

static int elks_aout_debug_enabled(void) {
    return cmdline_debug_enabled("perso:elks:aout");
}

static int SUB_NODISCARD elks_fail(int fd, int err, const char *msg) {
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

static int elks_map_object_pages(vm_map_t *map, pmap_t pmap, uint32_t start,
                                 uint32_t length, uint8_t prot,
                                 vm_object_t **obj_out) {
    uint32_t aligned_length;
    uint32_t va;
    vm_object_t *obj;

    if (obj_out) {
        *obj_out = NULL;
    }
    if (!map || !pmap || length == 0) {
        return 0;
    }

    aligned_length = (length + 0x0FFFU) & ~0x0FFFU;
    obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, aligned_length);
    if (!obj) {
        return -ENOMEM;
    }

    if (vm_map_insert(map, obj, 0, start, start + aligned_length,
                      prot, prot, VM_INHERIT_COPY) != 0) {
        vm_object_deallocate(obj);
        return -ENOMEM;
    }

    for (va = start; va < start + aligned_length; va += 0x1000U) {
        vm_page_t *page = vm_page_alloc(obj, (uint64_t)((va - start) >> 12), 0);
        void *page_kva;

        if (!page) {
            return -ENOMEM;
        }

        vm_object_add_page(obj, page);
        page_kva = (void *)(uintptr_t)(page->phys_addr + 0xC0000000U);
        memset(page_kva, 0, 0x1000U);

        if (pmap_enter(pmap, va, page->phys_addr, prot, 0) < 0) {
            return -ENOMEM;
        }
    }

    if (obj_out) {
        *obj_out = obj;
    }
    return 0;
}

static int elks_is_user_ptr(const void *ptr) {
    return (uintptr_t)ptr < 0xC0000000U;
}

static int elks_capture_ptr(char *const array[], int index, char **out) {
    if (elks_is_user_ptr(array)) {
        return copyin(&array[index], out, sizeof(char *));
    }
    *out = array[index];
    return 0;
}

static void elks_free_kernel_vector(char **vec) {
    size_t i;

    if (!vec) {
        return;
    }
    for (i = 0; vec[i]; i++) {
        size_t len = strlen(vec[i]) + 1U;
        kfree(vec[i], len);
    }
    kfree(vec, (i + 1U) * sizeof(char *));
}

static int elks_count_vector(char *const array[], int *count_out) {
    int count = 0;

    while (count < ELKS_ARG_MAX_COUNT) {
        char *item;

        if (!array) {
            break;
        }
        if (elks_capture_ptr(array, count, &item) != 0) {
            return -EFAULT;
        }
        if (!item) {
            break;
        }
        count++;
        if (count > (int)(ELKS_ARG_MAX_BYTES / 4)) {
            return -E2BIG;
        }
    }
    *count_out = count;
    return 0;
}

static int elks_dup_vector(char *const src[], char ***out_vec) {
    char **dst = NULL;
    int count = 0;
    int ret;
    int i;

    *out_vec = NULL;
    ret = elks_count_vector(src, &count);
    if (ret != 0) {
        return ret;
    }

    dst = kmalloc(((size_t)count + 1U) * sizeof(char *));
    if (!dst) {
        return -ENOMEM;
    }
    memset(dst, 0, ((size_t)count + 1U) * sizeof(char *));

    for (i = 0; i < count; i++) {
        char *item;
        size_t copied_len = 0;
        char *copy;

        if (elks_capture_ptr(src, i, &item) != 0) {
            ret = -EFAULT;
            goto fail;
        }
        if (!item) {
            break;
        }

        if (elks_is_user_ptr(item)) {
            ret = copyinstr(item, NULL, ELKS_ARG_MAX_BYTES, &copied_len);
            if (ret != 0) {
                ret = (ret == ENAMETOOLONG) ? -E2BIG : -EFAULT;
                goto fail;
            }
        } else {
            copied_len = strlen(item) + 1U;
        }

        copy = kmalloc(copied_len);
        if (!copy) {
            ret = -ENOMEM;
            goto fail;
        }

        if (elks_is_user_ptr(item)) {
            ret = copyinstr(item, copy, copied_len, NULL);
            if (ret != 0) {
                kfree(copy, copied_len);
                ret = (ret == ENAMETOOLONG) ? -E2BIG : -EFAULT;
                goto fail;
            }
        } else {
            memcpy(copy, item, copied_len);
        }

        dst[i] = copy;
    }

    *out_vec = dst;
    return 0;

fail:
    elks_free_kernel_vector(dst);
    return ret;
}

static int elks_dup_exec_vectors(char *const argv[], char *const envp[],
                                 char ***kargv_out, char ***kenvp_out) {
    int ret;

    *kargv_out = NULL;
    *kenvp_out = NULL;

    ret = elks_dup_vector(argv, kargv_out);
    if (ret != 0) {
        return ret;
    }
    ret = elks_dup_vector(envp, kenvp_out);
    if (ret != 0) {
        elks_free_kernel_vector(*kargv_out);
        *kargv_out = NULL;
        return ret;
    }
    return 0;
}

int SUB_NODISCARD SUB_NONNULL(2)
elks_check_file(const char *path, const char *header, size_t len) {
    (void)path;
    return elks_header_recognized(header, len) ? 0 : -ENOEXEC;
}

static int SUB_NODISCARD SUB_NONNULL(1, 2)
elks_setup_segments(process_t *proc, const struct elks_load_plan *plan) {
    struct elks_segment_layout layout;
    gdt_entry_t entries[ELKS_LDT_ES_INDEX + 1U];
    unsigned int entry_count = ELKS_LDT_ES_INDEX + 1U;

    memset(&layout, 0, sizeof(layout));
    memset(entries, 0, sizeof(entries));
    elks_build_segment_layout(plan, &layout);

    fill_ldt_entry(&entries[ELKS_LDT_CS_INDEX], &layout.cs);
    fill_ldt_entry(&entries[ELKS_LDT_DS_INDEX], &layout.ds);
    fill_ldt_entry(&entries[ELKS_LDT_SS_INDEX], &layout.ss);
    fill_ldt_entry(&entries[ELKS_LDT_ES_INDEX], &layout.es);

    return ldt_replace_process(proc, entries, entry_count);
}

int elks_load(int fd, const char *path, char *const argv[], char *const envp[]) {
    struct elks_exec hdr;
    struct elks_supl_hdr suph;
    struct elks_load_plan plan;
    vm_map_t *map = NULL;
    char **kargv = NULL;
    char **kenvp = NULL;
    size_t argv_envp_bytes;
    int ret;
    
    if (elks_aout_debug_enabled()) {
        kprint("ELKS: loading ");
        kprint(path ? path : "(null)");
        kprint("\n");
    }

    if (fd >= 0 && fd < MAX_FD && current_process && current_process->fds[fd]) {
        current_process->fds[fd]->f_offset = 0;
    } else {
        kern_lseek(fd, 0, 0);
    }
    {
        int rc = kern_read(fd, (char *)&hdr, sizeof(hdr));
        int status = elks_read_exact_status(rc, sizeof(hdr));

        if (status != 0) {
            return elks_fail(fd, status == -ENOEXEC ? -ENOEXEC : -EIO,
                             "ELKS: failed to read executable header");
        }
    }
    memset(&suph, 0, sizeof(suph));
    memset(&plan, 0, sizeof(plan));
    ret = elks_dup_exec_vectors(argv, envp, &kargv, &kenvp);
    if (ret != 0) {
        return elks_fail(fd, ret, "ELKS: failed to copy exec argument vectors");
    }
    argv_envp_bytes = elks_stack_image_bytes(kargv, kenvp);
    if (argv_envp_bytes > 0xFFFFU) {
        elks_free_kernel_vector(kargv);
        elks_free_kernel_vector(kenvp);
        return elks_fail(fd, -E2BIG, "ELKS: argv/envp image exceeds 16-bit segment");
    }
    if (!elks_header_recognized(&hdr, sizeof(hdr))) {
        elks_free_kernel_vector(kargv);
        elks_free_kernel_vector(kenvp);
        return elks_fail(fd, -ENOEXEC, "ELKS: header not recognized");
    }
    if (hdr.hlen > sizeof(hdr)) {
        size_t extra = (size_t)hdr.hlen - sizeof(hdr);

        if (extra > sizeof(suph)) {
            elks_free_kernel_vector(kargv);
            elks_free_kernel_vector(kenvp);
            return elks_fail(fd, -ENOEXEC, "ELKS: supplemental header too large");
        }
        {
            int rc = kern_read(fd, (char *)&suph, extra);
            int status = elks_read_exact_status(rc, extra);

            if (status != 0) {
                elks_free_kernel_vector(kargv);
                elks_free_kernel_vector(kenvp);
                return elks_fail(fd, status == -ENOEXEC ? -ENOEXEC : -EIO,
                                 "ELKS: failed to read supplemental header");
            }
        }
    }
    if (!elks_build_load_plan(&hdr, &suph, (uint16_t)argv_envp_bytes, &plan)) {
        elks_free_kernel_vector(kargv);
        elks_free_kernel_vector(kenvp);
        return elks_fail(fd, -ENOEXEC, "ELKS: invalid load plan");
    }
    
    // Create new address space
    extern pmap_t pmap_create(void);
    pmap_t pmap = pmap_create();
    if (!pmap) {
        elks_free_kernel_vector(kargv);
        elks_free_kernel_vector(kenvp);
        return elks_fail(fd, -ENOMEM, "ELKS: failed to create pmap");
    }
    
    current_process->pmap = (struct pmap*)pmap;
    extern void pmap_activate(pmap_t pmap);
    pmap_activate(pmap);
    map = vm_map_create(pmap, 0, 0xC0000000U);
    if (!map) {
        elks_free_kernel_vector(kargv);
        elks_free_kernel_vector(kenvp);
        return elks_fail(fd, -ENOMEM, "ELKS: failed to create vm_map");
    }

    if (plan.combined) {
        ret = elks_map_object_pages(map, pmap, plan.text_base, plan.text_limit,
                                    VM_PROT_READ | VM_PROT_EXEC | VM_PROT_WRITE,
                                    NULL);
        if (ret != 0) {
            elks_free_kernel_vector(kargv);
            elks_free_kernel_vector(kenvp);
            return elks_fail(fd, ret, "ELKS: failed to map combined text/data");
        }
    } else {
        ret = elks_map_object_pages(map, pmap, plan.text_base, plan.text_limit,
                                    VM_PROT_READ | VM_PROT_EXEC | VM_PROT_WRITE,
                                    NULL);
        if (ret != 0) {
            elks_free_kernel_vector(kargv);
            elks_free_kernel_vector(kenvp);
            return elks_fail(fd, ret, "ELKS: failed to map text");
        }

        ret = elks_map_object_pages(map, pmap, plan.data_base, plan.data_limit,
                                    VM_PROT_READ | VM_PROT_WRITE, NULL);
        if (ret != 0) {
            elks_free_kernel_vector(kargv);
            elks_free_kernel_vector(kenvp);
            return elks_fail(fd, ret, "ELKS: failed to map data/stack");
        }
    }

    if (plan.fartext_size > 0) {
        ret = elks_map_object_pages(map, pmap, plan.fartext_base, plan.fartext_size,
                                    VM_PROT_READ | VM_PROT_EXEC | VM_PROT_WRITE,
                                    NULL);
        if (ret != 0) {
            elks_free_kernel_vector(kargv);
            elks_free_kernel_vector(kenvp);
            return elks_fail(fd, ret, "ELKS: failed to map far text");
        }
    }
    
    // Read Text segment
    if (plan.text_size > 0) {
        kern_lseek(fd, (off_t)plan.text_file_offset, 0);
        {
            int rc = kern_read(fd, (void *)plan.text_base, plan.text_size);
            int status = elks_read_exact_status(rc, plan.text_size);

            if (status != 0) {
                elks_free_kernel_vector(kargv);
                elks_free_kernel_vector(kenvp);
                return elks_fail(fd, status == -ENOEXEC ? -ENOEXEC : -EIO,
                                 "ELKS: failed to read text segment");
            }
        }
    }

    if (plan.fartext_size > 0) {
        kern_lseek(fd, (off_t)plan.fartext_file_offset, 0);
        {
            int rc = kern_read(fd, (void *)plan.fartext_base, plan.fartext_size);
            int status = elks_read_exact_status(rc, plan.fartext_size);

            if (status != 0) {
                elks_free_kernel_vector(kargv);
                elks_free_kernel_vector(kenvp);
                return elks_fail(fd, status == -ENOEXEC ? -ENOEXEC : -EIO,
                                 "ELKS: failed to read far text segment");
            }
        }
    }

    if (plan.data_size > 0) {
        uint32_t data_load_base = plan.combined ? (plan.text_base + plan.text_size) : plan.data_base;

        kern_lseek(fd, (off_t)plan.data_file_offset, 0);
        {
            int rc = kern_read(fd, (void *)data_load_base, plan.data_size);
            int status = elks_read_exact_status(rc, plan.data_size);

            if (status != 0) {
                elks_free_kernel_vector(kargv);
                elks_free_kernel_vector(kenvp);
                return elks_fail(fd, status == -ENOEXEC ? -ENOEXEC : -EIO,
                                 "ELKS: failed to read data segment");
            }
        }
        memset((void *)(uintptr_t)(data_load_base + plan.data_size), 0, plan.bss_size);
    }
    
    // Setup LDT
    if (elks_setup_segments(current_process, &plan) != 0) {
        elks_free_kernel_vector(kargv);
        elks_free_kernel_vector(kenvp);
        return elks_fail(fd, -ENOMEM, "ELKS: failed to allocate or populate LDT");
    }
    ldt_activate(current_process);
    
    elks_apply_exec_state(current_process, &plan, path);
    proc_capture_cmdline(current_process, kargv);
    if (current_process->vm_map) {
        vm_map_destroy(current_process->vm_map);
    }
    current_process->vm_map = map;
    arch_set_kernel_stack((uintptr_t)current_thread->kstack_top);
    
    // Stack setup (simplified for now)
    // 16-bit stack points to end of Data segment
    uint16_t initial_sp = 0;
    uint32_t user_sp;
    struct elks_segment_layout layout;
    
    if (elks_aout_debug_enabled()) {
        kprint("ELKS: Loaded binary, jumping to 16-bit mode\n");
    }
    
    memset(&layout, 0, sizeof(layout));
    elks_build_segment_layout(&plan, &layout);
    if (!elks_build_stack_image((uint8_t *)(uintptr_t)plan.data_base, &plan,
                                kargv, kenvp, &initial_sp)) {
        elks_free_kernel_vector(kargv);
        elks_free_kernel_vector(kenvp);
        return elks_fail(fd, -E2BIG, "ELKS: failed to build startup stack image");
    }
    user_sp = initial_sp;

    extern void jump_to_elks(uint32_t entry, uint32_t stack, uint32_t cs,
                             uint32_t ds, uint32_t ss, uint32_t es);
    
    proc_close_cloexec(current_process);
    elks_free_kernel_vector(kargv);
    elks_free_kernel_vector(kenvp);
    kern_close(fd);

    /*
     * ELKS handoff runs after several VFS/kmalloc cleanup paths. Reassert the
     * freshly built user pmap and LDT immediately before the 16-bit iret so
     * the first instruction fetch cannot observe stale kernel address-space
     * state.
     */
    pmap_activate((pmap_t)(uintptr_t)current_process->pmap);
    ldt_activate(current_process);
    
    jump_to_elks(hdr.entry, user_sp ? user_sp : 0xFFFE, layout.cs_sel, layout.ds_sel,
                 layout.ss_sel, layout.es_sel);
    
    panic("jump_to_elks returned!");
    return 0;
}
