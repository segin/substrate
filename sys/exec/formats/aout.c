/*
 * a.out classifier and structural validator.  Pure C, no kernel deps —
 * the kernel-side loader (in this same file under !HOST_TEST) reads
 * the header off disk, calls these helpers, and then maps segments
 * via vm_map_insert.
 */

#include <exec/formats/aout.h>

#ifndef HOST_TEST
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/arch.h>
#include <sys/exec.h>
#include <sys/errno.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/param.h>
#include <sys/copy.h>
#include <sys/sysinfo.h>
#include <kern/cmdline.h>
#include <pm/pm.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_kmem.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <arch/i386/gdt.h>
#include <exec/perso/personality.h>
#include <stdio.h>
#endif

#include <string.h>

/* Per-segment cap (mirrors COFF's cap).  An a.out segment of more than
 * 1 GiB is corrupt or malicious. */
#define AOUT_MAX_SEGMENT_BYTES (1U << 30)

enum aout_flavor aout_classify(const struct aout_exec *hdr) {
    if (!hdr) return AOUT_FLAVOR_UNKNOWN;

    uint32_t magic = AOUT_GETMAGIC(hdr->a_midmag);
    uint32_t mid   = AOUT_GETMID(hdr->a_midmag);

    /* QMAGIC is exclusively Linux — no other flavour issues it.  This
     * shortcut is required by the spec because QMAGIC's first-page-unmapped
     * convention is incompatible with the BSD model that maps the header. */
    if (magic == AOUT_QMAGIC_VAL) {
        return AOUT_FLAVOR_LINUX;
    }

    /* Sanity: any subsequent flavour requires a recognised magic. */
    if (magic != AOUT_OMAGIC_VAL && magic != AOUT_NMAGIC_VAL &&
        magic != AOUT_ZMAGIC_VAL) {
        return AOUT_FLAVOR_UNKNOWN;
    }

    /* SunOS Sun386i has a unique MID and is the only Sun a.out the
     * i386 kernel can usefully run. */
    if (mid == AOUT_MID_SUN386) {
        return AOUT_FLAVOR_SUNOS;
    }

    /* BSD-style relocation tables are the strongest discriminator
     * between BSD and Linux: Linux dropped runtime relocations long
     * before its a.out era ended.  Non-zero a_trsize/a_drsize plus
     * a recognisable BSD MID -> NetBSD/OpenBSD. */
    int has_relocs = (hdr->a_trsize != 0 || hdr->a_drsize != 0);

    if (has_relocs) {
        if (mid == AOUT_MID_I386 || mid == AOUT_MID_M68K ||
            mid == AOUT_MID_SPARC) {
            /* NetBSD lineage (caller may refine to OpenBSD via brand). */
            return AOUT_FLAVOR_NETBSD;
        }
        if (mid == AOUT_MID_NONE || mid == AOUT_MID_I386_BSD) {
            /* Old FreeBSD tolerated host-endian and ignored MID. */
            return AOUT_FLAVOR_FREEBSD;
        }
        /* Unknown MID + relocations: treat as BSD-family unknown variant
         * rather than guessing Linux. */
        return AOUT_FLAVOR_UNKNOWN;
    }

    /* No relocations: most likely Linux statically-linked, possibly
     * stripped BSD.  Default to Linux per spec. */
    return AOUT_FLAVOR_LINUX;
}

int aout_validate_header(const struct aout_exec *hdr, uint32_t file_size) {
    if (!hdr) return -1;

    uint32_t magic = AOUT_GETMAGIC(hdr->a_midmag);
    if (magic != AOUT_OMAGIC_VAL && magic != AOUT_NMAGIC_VAL &&
        magic != AOUT_ZMAGIC_VAL && magic != AOUT_QMAGIC_VAL) {
        return -1;
    }

    /* Per-segment caps.  The header sizes are unsigned, so we just
     * upper-bound them.  Negative-as-int-32 nonsense in the file would
     * already fail this check because we compare against a positive cap. */
    if (hdr->a_text > AOUT_MAX_SEGMENT_BYTES) return -1;
    if (hdr->a_data > AOUT_MAX_SEGMENT_BYTES) return -1;
    if (hdr->a_bss  > AOUT_MAX_SEGMENT_BYTES) return -1;
    if (hdr->a_syms > AOUT_MAX_SEGMENT_BYTES) return -1;
    if (hdr->a_trsize > AOUT_MAX_SEGMENT_BYTES) return -1;
    if (hdr->a_drsize > AOUT_MAX_SEGMENT_BYTES) return -1;

    /* Combined virtual footprint (text+data+bss) must fit in 32-bit
     * arithmetic; reject overflows that would manifest as wrap. */
    uint32_t img = hdr->a_text + hdr->a_data;
    if (img < hdr->a_text) return -1;
    img += hdr->a_bss;
    if (img < hdr->a_bss) return -1;

    /* Entry-point sanity.  Only QMAGIC guarantees a non-zero text VA
     * (N_TXTADDR == PAGE_SIZE, with the first page unmapped), so a zero
     * entry there is invalid.  Linux ZMAGIC uses N_TXTADDR == 0 and links
     * crt0 first, so a_entry == 0 is the normal `_start` address; OMAGIC/
     * NMAGIC likewise start text at offset 0.  Only QMAGIC rejects it. */
    if (magic == AOUT_QMAGIC_VAL && hdr->a_entry == 0) {
        return -1;
    }

    /* File-size sanity: header + text + data + symtab + relocs must fit.
     * BSS is zero-fill so it doesn't consume file bytes. */
    uint32_t needed = AOUT_HEADER_SIZE;
    needed += hdr->a_text;     if (needed < hdr->a_text) return -1;
    needed += hdr->a_data;     if (needed < hdr->a_data) return -1;
    needed += hdr->a_syms;     if (needed < hdr->a_syms) return -1;
    needed += hdr->a_trsize;   if (needed < hdr->a_trsize) return -1;
    needed += hdr->a_drsize;   if (needed < hdr->a_drsize) return -1;
    if (needed > file_size) return -1;

    return 0;
}

#ifndef HOST_TEST

/*
 * Kernel-side a.out loader.  Registered with exec_dispatch so any image
 * whose first 4 bytes encode a recognised magic is handed to us.  The
 * loader is intentionally minimal: it maps text RX, data RW, and BSS
 * zero-filled, but does *not* yet apply relocations or set up a Linux/
 * BSD-flavoured personality — those still need to be plumbed before
 * userspace a.out binaries can actually execute.
 */

static int aout_check(const char *path, const char *header_buf, size_t len) {
    (void)path;
    if (len < AOUT_HEADER_SIZE) return -1;
    /* Avoid any alignment surprises: the on-disk header is little-endian
     * 32-bit and the host is i386 little-endian, so a memcpy is safe. */
    struct aout_exec hdr;
    memcpy(&hdr, header_buf, sizeof(hdr));
    uint32_t magic = AOUT_GETMAGIC(hdr.a_midmag);
    if (magic == AOUT_OMAGIC_VAL || magic == AOUT_NMAGIC_VAL ||
        magic == AOUT_ZMAGIC_VAL || magic == AOUT_QMAGIC_VAL) {
        return 0;
    }
    return -1;
}

#define ARG_MAX_COUNT   4096
#define ARG_STR_MAX     4096

#define AOUT_PAGE       0x1000U
#define AOUT_PAGE_MASK  (AOUT_PAGE - 1U)
#define AOUT_ROUND_UP(x) (((x) + AOUT_PAGE_MASK) & ~AOUT_PAGE_MASK)
#define AOUT_USER_MAX    0xC0000000U
#define AOUT_PHYS_LIMIT  0x3EC00000U     /* kernel direct-map ceiling */

static int aout_debug_enabled(void) {
    return cmdline_debug_enabled("perso:linux:aout");
}

static int aout_is_user_ptr(const void *p) {
    return (uintptr_t)p < AOUT_USER_MAX;
}

/*
 * Copy an execve argument vector into freshly kmalloc'd kernel storage before
 * the address space is torn down.  Handles both fully-user vectors (normal
 * execve) and kernel-constructed ones (init / #! interpreters), whose element
 * pointers may themselves be kernel or user.  Caller frees via aout_free_vec.
 */
static int aout_dup_vector(char *const src[], char ***out, int *count_out) {
    int n = 0;
    char **kv;

    *out = NULL;
    *count_out = 0;
    if (src) {
        while (n < ARG_MAX_COUNT) {
            char *e;
            if (aout_is_user_ptr(src)) {
                if (copyin(&src[n], &e, sizeof(e)) != 0) {
                    return -EFAULT;
                }
            } else {
                e = src[n];
            }
            if (!e) {
                break;
            }
            n++;
        }
    }

    kv = kmalloc(sizeof(char *) * (size_t)(n + 1));
    if (!kv) {
        return -ENOMEM;
    }
    for (int i = 0; i < n; i++) {
        char *e;
        char buf[ARG_STR_MAX];
        size_t copied = 0;

        if (aout_is_user_ptr(src)) {
            copyin(&src[i], &e, sizeof(e));
        } else {
            e = src[i];
        }
        if (aout_is_user_ptr(e)) {
            if (copyinstr(e, buf, sizeof(buf), &copied) != 0) {
                buf[0] = '\0';
                copied = 1;
            }
        } else {
            strlcpy(buf, e, sizeof(buf));
            copied = strlen(buf) + 1;
        }
        kv[i] = kmalloc(copied);
        if (!kv[i]) {
            for (int j = 0; j < i; j++) {
                kfree(kv[j], strlen(kv[j]) + 1);
            }
            kfree(kv, sizeof(char *) * (size_t)(n + 1));
            return -ENOMEM;
        }
        memcpy(kv[i], buf, copied);
    }
    kv[n] = NULL;
    *out = kv;
    *count_out = n;
    return 0;
}

static void aout_free_vector(char **kv, int n) {
    if (!kv) {
        return;
    }
    for (int i = 0; i < n; i++) {
        if (kv[i]) {
            kfree(kv[i], strlen(kv[i]) + 1);
        }
    }
    kfree(kv, sizeof(char *) * (size_t)(n + 1));
}

/*
 * Map [va, va+memsz) as an anonymous region and load `filesz` bytes from the
 * file at `foff` into its front; the remainder (bss) is demand-zero.  The
 * file-backed pages are eager-backed so the kern_read writes to present pages;
 * the trailing bss faults in on demand (VM_OBJ_TYPE_DEFAULT zero-fill).
 */
static int aout_map_region(pmap_t pmap, vm_map_t *map, uint32_t va,
                           uint32_t filesz, uint32_t memsz, uint8_t prot,
                           int fd, uint32_t foff) {
    uint32_t start = va & ~AOUT_PAGE_MASK;
    uint32_t end = AOUT_ROUND_UP(va + memsz);
    uint32_t len = end - start;
    uint32_t file_end = AOUT_ROUND_UP(va + filesz);
    vm_object_t *obj;
    uint32_t o;

    if (len == 0) {
        return 0;
    }
    obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, len);
    if (!obj) {
        return -ENOMEM;
    }
    if (vm_map_insert(map, obj, 0, start, start + len, prot, prot,
                      VM_INHERIT_COPY) != 0) {
        vm_object_deallocate(obj);
        return -ENOMEM;
    }

    /* Eager-back the file-backed pages so kern_read hits present memory. */
    for (o = start; o < file_end; o += AOUT_PAGE) {
        vm_page_t *page = vm_page_alloc(obj, (uint64_t)((o - start) >> 12), 0);
        void *kva;
        if (!page) {
            return -ENOMEM;
        }
        vm_object_add_page(obj, page);
        if (page->phys_addr >= AOUT_PHYS_LIMIT) {
            return -ENOMEM;
        }
        kva = (void *)(uintptr_t)(page->phys_addr + 0xC0000000U);
        memset(kva, 0, AOUT_PAGE);
        if (pmap_enter(pmap, o, page->phys_addr, prot, 0) < 0) {
            return -ENOMEM;
        }
    }

    if (filesz > 0) {
        kern_lseek(fd, (off_t)foff, 0);
        if (kern_read(fd, (void *)(uintptr_t)va, (int)filesz) != (int)filesz) {
            return -EIO;
        }
    }
    return 0;
}

/*
 * Build the Linux/i386 a.out startup stack at the top of the user address
 * space:  esp -> argc, argv[], NULL, envp[], NULL.  (a.out predates the ELF
 * auxiliary vector; crt0 stops at the envp NULL.)  Returns the initial esp.
 */
static int aout_build_stack(pmap_t pmap, char **kargv, int argc,
                            char **kenvp, int envc, uint32_t *sp_out) {
    const uint32_t top = AOUT_USER_MAX;
    const uint32_t eager = 32;                 /* 128 KiB mapped up front */
    uint32_t base = top - eager * AOUT_PAGE;
    /* argv/envp user-pointer arrays are heap-allocated: at ARG_MAX_COUNT
     * entries they are 16 KiB each, far too large for the 16 KiB kernel
     * stack (a real environment overflowed it and corrupted the return
     * address -> return to 0xffffffff). */
    uint32_t *argv_ua = kmalloc(sizeof(uint32_t) * (size_t)(argc + 1));
    uint32_t *envp_ua = kmalloc(sizeof(uint32_t) * (size_t)(envc + 1));
    uint32_t sp;
    int i;

    if (!argv_ua || !envp_ua) {
        if (argv_ua) kfree(argv_ua, sizeof(uint32_t) * (size_t)(argc + 1));
        if (envp_ua) kfree(envp_ua, sizeof(uint32_t) * (size_t)(envc + 1));
        return -ENOMEM;
    }

    for (uint32_t i2 = 0; i2 < eager; i2++) {
        void *pa = pmm_alloc_block();
        uint32_t phys;
        if (!pa) {
            goto oom;
        }
        phys = (uint32_t)(uintptr_t)pa - 0xC0000000U;
        if (pmap_enter(pmap, base + i2 * AOUT_PAGE, phys, VM_PROT_WRITE, 0) < 0) {
            pmm_free_block(pa);
            goto oom;
        }
        memset(pa, 0, AOUT_PAGE);
    }
    if (current_process) {
        current_process->ustack_top = top;
        current_process->ustack_limit = top - USER_STACK_MAX;
    }

    /* Strings first, growing down from the top. */
    sp = top;
    for (i = envc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(kenvp[i]) + 1U;
        sp -= len;
        memcpy((void *)(uintptr_t)sp, kenvp[i], len);
        envp_ua[i] = sp;
    }
    for (i = argc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(kargv[i]) + 1U;
        sp -= len;
        memcpy((void *)(uintptr_t)sp, kargv[i], len);
        argv_ua[i] = sp;
    }
    if (current_process && argc > 0) {
        current_process->arg_start = argv_ua[0];
        current_process->arg_end = top;
    }

    /*
     * Old Linux a.out uses the *indirect* startup convention: the kernel
     * passes argc and POINTERS to the argv/envp arrays, not the flat inline
     * layout ELF uses.  crt0/__libc_init read envp directly at [esp+8], so it
     * must be a char** there.  Lay out (high->low): the envp[] array, the
     * argv[] array, then [argc][argv][envp] at the very top of the frame.
     */
    sp &= ~0x3U;
    sp -= (uint32_t)(envc + 1) * 4U;
    uint32_t envp_arr = sp;
    for (i = 0; i < envc; i++) {
        *(uint32_t *)(uintptr_t)(envp_arr + (uint32_t)i * 4U) = envp_ua[i];
    }
    *(uint32_t *)(uintptr_t)(envp_arr + (uint32_t)envc * 4U) = 0;

    sp -= (uint32_t)(argc + 1) * 4U;
    uint32_t argv_arr = sp;
    for (i = 0; i < argc; i++) {
        *(uint32_t *)(uintptr_t)(argv_arr + (uint32_t)i * 4U) = argv_ua[i];
    }
    *(uint32_t *)(uintptr_t)(argv_arr + (uint32_t)argc * 4U) = 0;

    sp &= ~0xFU;
    sp -= 12U;
    {
        uint32_t *v = (uint32_t *)(uintptr_t)sp;
        v[0] = (uint32_t)argc;
        v[1] = argv_arr;
        v[2] = envp_arr;
    }

    *sp_out = sp;
    kfree(argv_ua, sizeof(uint32_t) * (size_t)(argc + 1));
    kfree(envp_ua, sizeof(uint32_t) * (size_t)(envc + 1));
    return 0;

oom:
    kfree(argv_ua, sizeof(uint32_t) * (size_t)(argc + 1));
    kfree(envp_ua, sizeof(uint32_t) * (size_t)(envc + 1));
    return -ENOMEM;
}

static int aout_load(int fd, const char *path, char *const argv[],
                     char *const envp[]) {
    struct aout_exec hdr;
    char **kargv = NULL, **kenvp = NULL;
    int argc = 0, envc = 0;
    uint32_t magic;
    uint32_t txtaddr, txtoff, dataddr, dataoff, bssaddr, brk;
    uint8_t text_prot;
    pmap_t pmap;
    vm_map_t *map;
    uint32_t sp;
    int rc;

    kern_lseek(fd, 0, 0);
    if (kern_read(fd, (char *)&hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        kern_close(fd);
        return -ENOEXEC;
    }
    {
        int64_t end = kern_lseek(fd, 0, 2 /* SEEK_END */);
        if (end <= 0 || end > (int64_t)0xFFFFFFFFLL ||
            aout_validate_header(&hdr, (uint32_t)end) != 0) {
            kern_close(fd);
            return -ENOEXEC;
        }
    }

    /* This loader targets Linux a.out (ZMAGIC/QMAGIC/OMAGIC/NMAGIC).  BSD and
     * SunOS flavours need relocation/interpreter handling not done here. */
    if (aout_classify(&hdr) != AOUT_FLAVOR_LINUX) {
        kern_close(fd);
        return -ENOEXEC;
    }

    magic = AOUT_GETMAGIC(hdr.a_midmag);
    switch (magic) {
    case AOUT_ZMAGIC_VAL:
        txtaddr = 0; txtoff = 1024;
        dataddr = AOUT_ROUND_UP(hdr.a_text);
        text_prot = VM_PROT_READ | VM_PROT_EXEC;
        break;
    case AOUT_QMAGIC_VAL:
        txtaddr = AOUT_PAGE; txtoff = 0;
        dataddr = AOUT_PAGE + AOUT_ROUND_UP(hdr.a_text);
        text_prot = VM_PROT_READ | VM_PROT_EXEC;
        break;
    default: /* OMAGIC / NMAGIC: contiguous, writable text */
        txtaddr = 0; txtoff = sizeof(struct aout_exec);
        dataddr = (magic == AOUT_OMAGIC_VAL)
                      ? txtaddr + hdr.a_text
                      : AOUT_ROUND_UP(hdr.a_text);
        text_prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC;
        break;
    }
    dataoff = txtoff + hdr.a_text;
    bssaddr = dataddr + hdr.a_data;
    brk = bssaddr + hdr.a_bss;

    if (aout_debug_enabled()) {
        char b[128];
        snprintf(b, sizeof(b),
                 "a.out: %s magic=0%o entry=0x%x txt@0x%x(f%u,%u) dat@0x%x(f%u,%u) bss@0x%x+%u\n",
                 path ? path : "?", magic, hdr.a_entry, txtaddr, txtoff,
                 hdr.a_text, dataddr, dataoff, hdr.a_data, bssaddr, hdr.a_bss);
        kprint(b);
    }

    /* Copy argv/envp before the old address space goes away. */
    rc = aout_dup_vector(argv, &kargv, &argc);
    if (rc != 0) {
        kern_close(fd);
        return rc;
    }
    rc = aout_dup_vector(envp, &kenvp, &envc);
    if (rc != 0) {
        aout_free_vector(kargv, argc);
        kern_close(fd);
        return rc;
    }

    pmap = pmap_create();
    if (!pmap) {
        aout_free_vector(kargv, argc);
        aout_free_vector(kenvp, envc);
        kern_close(fd);
        return -ENOMEM;
    }
    current_process->pmap = (struct pmap *)pmap;
    pmap_activate(pmap);
    /* Linux ZMAGIC/OMAGIC place text at virtual address 0, so the user vm_map
     * must start at 0 (QMAGIC still leaves page 0 unmapped as its NULL guard). */
    map = vm_map_create(pmap, 0, AOUT_USER_MAX);
    if (!map) {
        aout_free_vector(kargv, argc);
        aout_free_vector(kenvp, envc);
        kern_close(fd);
        return -ENOMEM;
    }

    if (magic == AOUT_OMAGIC_VAL || magic == AOUT_NMAGIC_VAL) {
        /* Text + data are contiguous; map as one region, then extend for bss. */
        rc = aout_map_region(pmap, map, txtaddr, hdr.a_text + hdr.a_data,
                             hdr.a_text + hdr.a_data + hdr.a_bss, text_prot,
                             fd, txtoff);
        if (rc == 0 && hdr.a_data > 0) {
            kern_lseek(fd, (off_t)dataoff, 0);
            if (kern_read(fd, (void *)(uintptr_t)dataddr, (int)hdr.a_data)
                != (int)hdr.a_data) {
                rc = -EIO;
            }
        }
    } else {
        rc = aout_map_region(pmap, map, txtaddr, hdr.a_text, hdr.a_text,
                             text_prot, fd, txtoff);
        if (rc == 0) {
            rc = aout_map_region(pmap, map, dataddr, hdr.a_data,
                                 hdr.a_data + hdr.a_bss,
                                 VM_PROT_READ | VM_PROT_WRITE, fd, dataoff);
        }
    }
    if (rc != 0) {
        aout_free_vector(kargv, argc);
        aout_free_vector(kenvp, envc);
        kern_close(fd);
        return rc;
    }

    /* POSIX: caught signal handlers are reset to SIG_DFL across exec.  Without
     * this the a.out image inherits the previous image's handler pointers (e.g.
     * the shell's SIGCHLD handler) and jumps into now-unmapped code the first
     * time such a signal is delivered. */
    proc_exec_reset_signals();

    /* Process state. */
    current_process->perso_id = PERS_LINUX;
    current_process->bitness = BITNESS_32;
    /* ZMAGIC/OMAGIC map text at VA 0, so pointers into the first page (e.g. a
     * uselib() path string in .text) are valid — let copyin/copyinstr accept
     * the low region for this process. */
    current_process->low_va_valid = (txtaddr == 0) ? 1 : 0;
    current_process->brk_start = AOUT_ROUND_UP(brk);
    current_process->brk = current_process->brk_start;
    {
        const char *name = path ? path : "";
        const char *p;
        for (p = name; *p; p++) {
            if (*p == '/') {
                name = p + 1;
            }
        }
        strlcpy(current_process->comm, name, sizeof(current_process->comm));
        if (path) {
            strlcpy(current_process->exec_path, path,
                    sizeof(current_process->exec_path));
        } else {
            current_process->exec_path[0] = '\0';
        }
    }

    if (current_process->vm_map) {
        vm_map_destroy(current_process->vm_map);
    }
    current_process->vm_map = map;
    arch_set_kernel_stack((uintptr_t)current_thread->kstack_top);

    rc = aout_build_stack(pmap, kargv, argc, kenvp, envc, &sp);
    aout_free_vector(kargv, argc);
    aout_free_vector(kenvp, envc);
    if (rc != 0) {
        kern_close(fd);
        return rc;
    }

    proc_close_cloexec(current_process);
    kern_close(fd);

    if (aout_debug_enabled()) {
        char b[64];
        snprintf(b, sizeof(b), "a.out: enter eip=0x%x esp=0x%x\n",
                 hdr.a_entry, sp);
        kprint(b);
    }

    pmap_activate((pmap_t)(uintptr_t)current_process->pmap);
    jump_to_userspace(hdr.a_entry, sp, 0);
    return 0;   /* not reached */
}

/*
 * uselib(2) — load an old-style Linux a.out shared library (e.g. libc.so.4)
 * into the caller's address space.  Unlike an executable, a shared library
 * carries its fixed load address in a_entry (libc.so.4 -> 0x60000000,
 * /lib/ld.so -> 0x62f00000); the crt0 of a dynamically-linked a.out binary
 * calls uselib() for each library, then jumps into the fixed jump-table
 * addresses baked into the executable.  We map the library's text (RX), data
 * (RW) and bss (zero) at that address in the current pmap — no new address
 * space, no transfer of control.
 */
static int aout_load_library(const char *path) {
    struct aout_exec hdr;
    uint32_t magic, load, txtoff, dataddr, dataoff, brk;
    pmap_t pmap;
    vm_map_t *map;
    int fd, rc;

    if (!current_process || !current_process->vm_map) {
        return -EINVAL;
    }
    fd = kern_open(path, 0 /* O_RDONLY */, 0);
    if (fd < 0) {
        return fd;
    }
    if (kern_read(fd, (char *)&hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        kern_close(fd);
        return -ENOEXEC;
    }
    {
        int64_t end = kern_lseek(fd, 0, 2);
        if (end <= 0 || end > (int64_t)0xFFFFFFFFLL ||
            aout_validate_header(&hdr, (uint32_t)end) != 0) {
            kern_close(fd);
            return -ENOEXEC;
        }
    }

    magic = AOUT_GETMAGIC(hdr.a_midmag);
    /* A shared library carries its fixed load address in a_entry. */
    load = hdr.a_entry & ~AOUT_PAGE_MASK;
    if (load == 0) {
        kern_close(fd);
        return -ENOEXEC;
    }
    if (magic == AOUT_ZMAGIC_VAL) {
        txtoff = 1024;
        dataddr = load + AOUT_ROUND_UP(hdr.a_text);
    } else if (magic == AOUT_QMAGIC_VAL) {
        txtoff = 0;
        dataddr = load + AOUT_ROUND_UP(hdr.a_text);
    } else { /* OMAGIC/NMAGIC */
        txtoff = sizeof(struct aout_exec);
        dataddr = (magic == AOUT_OMAGIC_VAL) ? load + hdr.a_text
                                             : load + AOUT_ROUND_UP(hdr.a_text);
    }
    dataoff = txtoff + hdr.a_text;
    brk = dataddr + hdr.a_data + hdr.a_bss;
    (void)brk;

    pmap = (pmap_t)current_process->pmap;
    map = current_process->vm_map;

    if (magic == AOUT_OMAGIC_VAL || magic == AOUT_NMAGIC_VAL) {
        rc = aout_map_region(pmap, map, load, hdr.a_text + hdr.a_data,
                             hdr.a_text + hdr.a_data + hdr.a_bss,
                             VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC,
                             fd, txtoff);
        if (rc == 0 && hdr.a_data > 0) {
            kern_lseek(fd, (off_t)dataoff, 0);
            if (kern_read(fd, (void *)(uintptr_t)dataddr, (int)hdr.a_data)
                != (int)hdr.a_data) {
                rc = -EIO;
            }
        }
    } else {
        rc = aout_map_region(pmap, map, load, hdr.a_text, hdr.a_text,
                             VM_PROT_READ | VM_PROT_EXEC, fd, txtoff);
        if (rc == 0) {
            rc = aout_map_region(pmap, map, dataddr, hdr.a_data,
                                 hdr.a_data + hdr.a_bss,
                                 VM_PROT_READ | VM_PROT_WRITE, fd, dataoff);
        }
    }

    if (aout_debug_enabled()) {
        char b[96];
        snprintf(b, sizeof(b), "a.out: uselib %s @0x%x rc=%d\n",
                 path ? path : "?", load, rc);
        kprint(b);
    }
    kern_close(fd);
    return rc;
}

/* Linux uselib(2) syscall entry (personality syscall-table slot). */
int aout_sys_uselib(uint32_t upath, uint32_t a1, uint32_t a2, uint32_t a3,
                    uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    char kpath[256];
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;

    if (upath == 0) {
        return -EFAULT;
    }
    if (copyinstr((const void *)(uintptr_t)upath, kpath, sizeof(kpath), NULL) != 0) {
        return -EFAULT;
    }
    return aout_load_library(kpath);
}

static struct exec_binary_handler aout_handler = {
    .name = "aout",
    .check = aout_check,
    .load = aout_load,
    .next = NULL,
};

void aout_init_handler(void) {
    exec_register_handler(&aout_handler);
}

#endif /* !HOST_TEST */
