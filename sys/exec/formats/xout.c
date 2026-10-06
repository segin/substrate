/*
 * xout.c - Loader for Microsoft x.out segmented executables (Xenix).
 *
 * Two loaders behind one handler: the 32-bit 80386 flavour first, the
 * 16-bit 8086/80286 flavour after it, and at the end the handler that
 * picks between them from the header's x_cpu.
 *
 * The 386 flavour:
 *
 * Loads a 386 segmented x.out image: each text/data segment is placed at its
 * own linear base and described by an LDT descriptor whose selector matches
 * the one baked into the binary (e.g. code selector 0x3f -> LDT entry 7).
 * Execution begins in 32-bit segmented mode at the entry segment; system
 * calls are trapped and emulated by the Xenix personality (perso_xenix.c),
 * which decodes the SysV/386 `lcall $7,$0` gate.
 *
 * Modeled on the ELKS a.out loader (elks_aout.c), which proves the segmented
 * LDT execution path; the difference here is 32-bit descriptors and a
 * data-driven segment table rather than a fixed 16-bit layout.
 */

#include <stdio.h>
#include <string.h>

#include <machine/fpu.h>
#include <machine/gdt.h>
#include <machine/pmap.h>
#include <machine/pmm.h>
#include <machine/vmparam.h>
#include <exec/formats/xout.h>
#include <exec/perso/personality.h>
#include <kern/arch.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <pm/pm.h>
#include <sys/compiler.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/kern_syscalls.h>
#include <sys/ldt.h>
#include <sys/proc.h>
#include <sys/sysinfo.h>
#include <vm/vm_kmem.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

/* Linear placement of the loaded segments.  Distinct, page-aligned windows
 * inside the user vm_map; the segment descriptor base is set to these so the
 * binary's own segment-relative (0-based) offsets resolve correctly. */
#define XOUT_TEXT_BASE   0x08000000U   /* 128 MiB */
#define XOUT_DATA_BASE   0x10000000U   /* 256 MiB */

/* Headroom appended to the data segment above bss for the initial stack and
 * early heap (brk grows the descriptor limit from here later). */
#define XOUT_STACK_SIZE  0x00100000U   /* 1 MiB */

#define XOUT_PAGE        0x1000U
#define XOUT_PAGE_MASK   (XOUT_PAGE - 1U)
#define XOUT_ROUND_UP(x) (((x) + XOUT_PAGE_MASK) & ~XOUT_PAGE_MASK)

static int xout_debug_enabled(void) {
    return cmdline_debug_enabled("perso:xenix:xout");
}

static int xout_fail(int fd, int err, const char *msg) {
    if (msg) {
        kprint(msg);
        kprint("\n");
    }
    if (fd >= 0) {
        kern_close(fd);
    }
    return err;
}

/* Map [start, start+length) as fresh zero-filled anonymous pages, prot-mapped
 * into the given vm_map.  Mirrors elks_map_object_pages including the direct-
 * map ceiling guard. */
static int xout_insert_region(vm_map_t *map, uint32_t start, uint32_t length,
                              uint8_t prot, vm_object_t **obj_out) {
    uint32_t aligned_length;
    vm_object_t *obj;

    *obj_out = NULL;
    if (!map || length == 0) {
        return 0;
    }

    aligned_length = XOUT_ROUND_UP(length);
    obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, aligned_length);
    if (!obj) {
        return -ENOMEM;
    }
    if (vm_map_insert(map, obj, 0, start, start + aligned_length,
                      prot, prot, VM_INHERIT_COPY) != 0) {
        vm_object_deallocate(obj);
        return -ENOMEM;
    }
    *obj_out = obj;
    return 0;
}

/* Eagerly back a sub-range [off, off+length) of an already-inserted region
 * with zeroed, pmap-entered pages.  Offsets not populated here fault in as
 * demand-zero anonymous pages -- so a 24 MiB data segment with a tiny image
 * costs only the pages we actually touch at load time. */
static int xout_populate(vm_map_t *map, pmap_t pmap, vm_object_t *obj,
                         uint32_t start, uint32_t off, uint32_t length,
                         uint8_t prot) {
    uint32_t o;

    if (!obj || length == 0) {
        return 0;
    }
    off &= ~XOUT_PAGE_MASK;
    for (o = off; o < off + XOUT_ROUND_UP(length); o += XOUT_PAGE) {
        uint32_t va = start + o;
        vm_page_t *page = vm_page_alloc(obj, (uint64_t)(o >> 12), 0);
        void *page_kva;

        if (!page) {
            return -ENOMEM;
        }
        vm_object_add_page(obj, page);
        if (!pmm_phys_is_direct_mapped(page->phys_addr)) {
            return -ENOMEM;
        }
        /* Zero via the kernel direct map -- the user VA is not mapped yet. */
        page_kva = P2V(page->phys_addr);
        memset(page_kva, 0, XOUT_PAGE);
        (void)map;
        if (pmap_enter(pmap, va, page->phys_addr, prot, 0) < 0) {
            return -ENOMEM;
        }
    }
    return 0;
}

/* Build a 32-bit LDT descriptor. `code` selects executable vs writable data. */
static void xout_fill_descriptor(gdt_entry_t *entry, uint32_t base,
                                 uint32_t byte_size, int code) {
    struct user_desc info;
    uint32_t pages = XOUT_ROUND_UP(byte_size) >> 12;

    memset(&info, 0, sizeof(info));
    info.base_addr = base;
    /* Page-granular limit: text alone exceeds the 1 MiB byte-granular cap. */
    info.limit = pages ? (pages - 1U) : 0U;
    info.limit_in_pages = 1;
    info.seg_32bit = 1;
    info.contents = code ? 2 : 0;   /* 2=code(exec/read), 0=data(read/write) */
    info.read_exec_only = 0;
    info.seg_not_present = 0;
    info.useable = 1;

    fill_ldt_entry(entry, &info);
}

/*
 * Build the initial SysV/386 process stack in the data segment:
 *
 *   [strings ...]           (highest addresses)
 *   NULL
 *   envp[n-1] .. envp[0]
 *   NULL
 *   argv[argc-1] .. argv[0]
 *   argc                    <- initial ESP (segment-relative offset)
 *
 * Pointers are segment-relative offsets into DS (== SS).  Returns the initial
 * ESP as an offset within the data segment.
 */
static uint32_t xout_build_stack(uint8_t *data_seg, uint32_t seg_size,
                                 char *const argv[], char *const envp[]) {
    int argc = 0, envc = 0;
    int i;
    uint32_t strtop = seg_size;   /* strings grow down from the segment top */
    uint32_t argv_off[64];
    uint32_t envp_off[64];

    /* The vectors hold the caller's pointers; fetch them at its width. */
    char *argp[64], *envpp[64];

    for (argc = 0; argc < 63 && argv; argc++) {
        if (exec_vec_ptr(argv, argc, &argp[argc]) != 0 || !argp[argc]) break;
    }
    for (envc = 0; envc < 63 && envp; envc++) {
        if (exec_vec_ptr(envp, envc, &envpp[envc]) != 0 || !envpp[envc]) break;
    }

    /* Copy strings into the top of the segment, recording their offsets. */
    for (i = argc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(argp[i]) + 1U;
        strtop -= len;
        memcpy(data_seg + strtop, argp[i], len);
        argv_off[i] = strtop;
    }
    for (i = envc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(envpp[i]) + 1U;
        strtop -= len;
        memcpy(data_seg + strtop, envpp[i], len);
        envp_off[i] = strtop;
    }

    /* Align the vector area to 4 bytes below the strings. */
    strtop &= ~0x3U;

    /* Total words: argc + argv[argc] + NULL + envp[envc] + NULL. */
    uint32_t words = 1U + (uint32_t)argc + 1U + (uint32_t)envc + 1U;
    uint32_t vec_off = strtop - words * 4U;
    uint32_t *vec = (uint32_t *)(data_seg + vec_off);
    uint32_t w = 0;

    vec[w++] = (uint32_t)argc;
    for (i = 0; i < argc; i++) {
        vec[w++] = argv_off[i];
    }
    vec[w++] = 0;
    for (i = 0; i < envc; i++) {
        vec[w++] = envp_off[i];
    }
    vec[w++] = 0;

    return vec_off;
}

static int xout_check_file(const char *path, const char *header, size_t len) {
    const struct xexec *hdr = (const struct xexec *)header;

    (void)path;
    if (!header || len < sizeof(struct xexec)) {
        return -ENOEXEC;
    }
    if (hdr->x_magic != XOUT_MAGIC) {
        return -ENOEXEC;
    }
    /* x.out covers the 8086/80286/80386 Xenix targets under one magic.  This
     * check is the 32-bit 386 one; the 16-bit segmented images belong to
     * x286_check_file below.  Older 386 linkers left x_cpu zero, so accept
     * that too. */
    if ((hdr->x_cpu & XC_CPU_MASK) != 0U &&
        (hdr->x_cpu & XC_CPU_MASK) != XC_80386) {
        return -ENOEXEC;
    }
    return 0;
}

static int xout_load(int fd, const char *path, char *const argv[],
                     char *const envp[]) {
    struct xexec hdr;
    struct xext ext;
    uint8_t segtab[XOUT_SEG_STRIDE * 64U];   /* up to 64 segments */
    unsigned int nsegs, stride;
    unsigned int max_ldt_index = 0;
    gdt_entry_t entries[64];
    pmap_t pmap;
    vm_map_t *map;
    uint16_t cs_sel = (uint16_t)0, ds_sel = 0;
    uint32_t data_base = 0, data_total = 0;
    uint32_t entry_off = 0, user_sp = 0;
    int rc;
    unsigned int s;

    /* --- headers --- */
    kern_lseek(fd, 0, 0);
    if (kern_read(fd, (char *)&hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        return xout_fail(fd, -ENOEXEC, "xout: short read on header");
    }
    if (hdr.x_magic != XOUT_MAGIC) {
        return xout_fail(fd, -ENOEXEC, "xout: bad magic");
    }
    if ((hdr.x_cpu & XC_CPU_MASK) != 0U &&
        (hdr.x_cpu & XC_CPU_MASK) != XC_80386) {
        return xout_fail(fd, -ENOEXEC, "xout: not an i386 image");
    }
    if (hdr.x_ext < sizeof(struct xext)) {
        return xout_fail(fd, -ENOEXEC, "xout: missing/short extension header");
    }
    if (kern_read(fd, (char *)&ext, sizeof(ext)) != (int)sizeof(ext)) {
        return xout_fail(fd, -ENOEXEC, "xout: short read on extension");
    }
    if (ext.xe_segpos <= 0 || ext.xe_segsize <= 0 ||
        (unsigned int)ext.xe_segsize > sizeof(segtab)) {
        return xout_fail(fd, -ENOEXEC, "xout: invalid segment table");
    }

    stride = XOUT_SEG_STRIDE;
    nsegs = (unsigned int)ext.xe_segsize / stride;
    if (nsegs == 0) {
        return xout_fail(fd, -ENOEXEC, "xout: empty segment table");
    }

    kern_lseek(fd, ext.xe_segpos, 0);
    if (kern_read(fd, (char *)segtab, (int)ext.xe_segsize) != ext.xe_segsize) {
        return xout_fail(fd, -EIO, "xout: short read on segment table");
    }

    if (xout_debug_enabled()) {
        char b[128];
        snprintf(b, sizeof(b),
                 "xout: %s magic=%04x renv=%04x eseg=%04x nsegs=%u entry=%x\n",
                 path ? path : "?", hdr.x_magic, hdr.x_renv, ext.xe_eseg,
                 nsegs, (unsigned int)hdr.x_entry);
        kprint(b);
    }

    /* Determine the largest LDT index we must materialize. */
    for (s = 0; s < nsegs; s++) {
        const struct xseg *seg = (const struct xseg *)(segtab + s * stride);
        unsigned int idx = XOUT_SEL_INDEX(seg->xs_seg);
        if (seg->xs_type != XS_TEXT && seg->xs_type != XS_DATA) {
            continue;
        }
        if (idx > max_ldt_index) {
            max_ldt_index = idx;
        }
    }
    if (max_ldt_index + 1U > (sizeof(entries) / sizeof(entries[0]))) {
        return xout_fail(fd, -ENOEXEC, "xout: segment selector out of range");
    }

    /* --- address space --- */
    pmap = pmap_create();
    if (!pmap) {
        return xout_fail(fd, -ENOMEM, "xout: pmap_create failed");
    }
    current_process->pmap = (struct pmap *)pmap;
    pmap_activate(pmap);
    map = vm_map_create(pmap, 0x10000, USER32_VA_END);
    if (!map) {
        return xout_fail(fd, -ENOMEM, "xout: vm_map_create failed");
    }

    memset(entries, 0, sizeof(entries));

    /* --- load each segment, build its descriptor --- */
    for (s = 0; s < nsegs; s++) {
        const struct xseg *seg = (const struct xseg *)(segtab + s * stride);
        unsigned int idx = XOUT_SEL_INDEX(seg->xs_seg);
        uint32_t vsize = (uint32_t)seg->xs_vsize;
        uint32_t psize = (uint32_t)seg->xs_psize;
        uint32_t msize = (uint32_t)seg->xs_msize;
        uint32_t base, total;
        uint8_t prot;
        vm_object_t *obj = NULL;
        int is_code;

        if (seg->xs_type == XS_TEXT) {
            is_code = 1;
            base = XOUT_TEXT_BASE;
            total = XOUT_ROUND_UP(vsize > psize ? vsize : psize);
            prot = VM_PROT_READ | VM_PROT_EXEC | VM_PROT_WRITE;
        } else if (seg->xs_type == XS_DATA) {
            uint32_t reserve = msize > vsize ? msize : vsize;
            is_code = 0;
            base = XOUT_DATA_BASE;
            /* The data segment spans data + bss + a large heap reservation
             * (xs_msize), plus stack headroom on top (SS == DS in the Xenix
             * small model). */
            total = XOUT_ROUND_UP(reserve) + XOUT_STACK_SIZE;
            data_base = base;
            data_total = total;
            ds_sel = seg->xs_seg;
            prot = VM_PROT_READ | VM_PROT_WRITE;
        } else {
            continue;   /* symbol / relocation segments: ignored for exec */
        }

        rc = xout_insert_region(map, base, total, prot, &obj);
        if (rc != 0) {
            return xout_fail(fd, rc, "xout: failed to map segment");
        }

        /* Eagerly back the on-disk image so kern_read writes to present pages;
         * the rest (bss / heap) demand-zeros. */
        if (psize > 0) {
            rc = xout_populate(map, pmap, obj, base, 0, psize, prot);
            if (rc != 0) {
                return xout_fail(fd, rc, "xout: failed to back segment image");
            }
        }
        /* Eagerly back the stack tail so the startup stack image can be built
         * through the user VA. */
        if (!is_code && total > XOUT_STACK_SIZE) {
            uint32_t tail = 0x10000U;   /* 64 KiB */
            rc = xout_populate(map, pmap, obj, base, total - tail, tail, prot);
            if (rc != 0) {
                return xout_fail(fd, rc, "xout: failed to back stack tail");
            }
        }

        if (psize > 0 && seg->xs_filpos > 0) {
            int got;
            kern_lseek(fd, seg->xs_filpos, 0);
            got = kern_read(fd, (void *)(uintptr_t)base, (int)psize);
            if (got < 0) {
                return xout_fail(fd, -EIO, "xout: read error on segment image");
            }
            /* A short read (e.g. a truncated `split` fragment) leaves the tail
             * zero-filled, which is the same state bss expects; tolerate it so
             * a fully-present text segment can still be entered. */
            if ((uint32_t)got < psize && xout_debug_enabled()) {
                char b[96];
                snprintf(b, sizeof(b),
                         "xout: seg sel=%04x short read %d/%u (truncated image)\n",
                         seg->xs_seg, got, psize);
                kprint(b);
            }
        }

        xout_fill_descriptor(&entries[idx], base, total, is_code);

        if (seg->xs_seg == ext.xe_eseg) {
            cs_sel = seg->xs_seg;
            entry_off = (uint32_t)hdr.x_entry;
        }
    }

    if (cs_sel == 0 || ds_sel == 0) {
        return xout_fail(fd, -ENOEXEC, "xout: missing entry or data segment");
    }

    /* --- install the LDT --- */
    if (ldt_replace_process(current_process, entries, max_ldt_index + 1U) != 0) {
        return xout_fail(fd, -ENOMEM, "xout: ldt_replace_process failed");
    }
    ldt_activate(current_process);

    /* --- process state --- */
    current_process->perso_id = PERS_XENIX;
    current_process->bitness = BITNESS_32;
    current_process->brk_start = XOUT_ROUND_UP((uint32_t)hdr.x_data +
                                               (uint32_t)hdr.x_bss);
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
    /* The new image starts with the FPU in its initial state. */
    fpu_thread_reset(current_thread);
    arch_set_kernel_stack((uintptr_t)current_thread->kstack_top);

    /* What ps and /proc/<pid>/cmdline report: the new argv, not the argv
     * snapshot and argv-region bounds inherited from the parent at fork.
     * The vector still holds the caller's pointers, as xout_build_stack
     * reads them below. */
    {
        char *cmd_argv[64];
        int n;

        for (n = 0; n < 63 && argv; n++) {
            if (exec_vec_ptr(argv, n, &cmd_argv[n]) != 0 || !cmd_argv[n]) break;
        }
        cmd_argv[n] = NULL;
        current_process->arg_start = 0;
        current_process->arg_end = 0;
        proc_capture_cmdline(current_process, cmd_argv);
    }

    /* --- initial stack (built directly in the data segment) --- */
    user_sp = xout_build_stack((uint8_t *)(uintptr_t)data_base, data_total,
                               argv, envp);

    proc_close_cloexec(current_process);
    kern_close(fd);

    if (xout_debug_enabled()) {
        char b[128];
        snprintf(b, sizeof(b),
                 "xout: enter cs=%04x:%08x ss=%04x:%08x\n",
                 cs_sel, entry_off, ds_sel, user_sp);
        kprint(b);
    }

    /* Reassert the freshly built pmap/LDT immediately before handoff. */
    pmap_activate((pmap_t)(uintptr_t)current_process->pmap);
    ldt_activate(current_process);

    jump_to_elks(entry_off, user_sp, cs_sel, ds_sel, ds_sel, ds_sel, 0);

    return 0;   /* not reached */
}

/* =====================================================================
 * The 16-bit half: 8086 and 80286 x.out images.
 *
 * These are 16-bit protected-mode programs: SCO Xenix/286 gave each x.out
 * segment its own LDT descriptor, and the linker baked the resulting
 * selectors straight into the image (0x3f, 0x47, 0x4f, ... -- LDT slots 7,
 * 8, 9, ... with TI=1, RPL=3).  A middle-model binary like Microsoft Word
 * 3.0 therefore arrives as six code segments plus two data segments, and
 * reaches between them with `lcall $0x47,$off` rather than a near call.
 *
 * We reproduce that faithfully rather than flattening it: each segment is
 * mapped into its own naturally-aligned 64 KiB linear window, and the LDT
 * slot the binary expects is filled with a 16-bit (D/B=0), byte-granular
 * descriptor whose base is that window.  Every offset the program computes
 * is then correct by construction, and an out-of-range one still faults the
 * way it did on real hardware.
 *
 * The first XS_DATA segment is DGROUP: DS == ES == SS, holding initialized
 * data, bss, the malloc arena (grown via brkctl(2), see perso_xenix.c)
 * and the process stack at its top.  Xenix sized that segment to 64 KiB and
 * grew break and stack toward each other, and so do we.
 *
 * Execution begins in 16-bit mode; system calls trap out through `int $5`
 * and are emulated by the 16-bit half of the Xenix personality.
 */

#define X286_PAGE        0x1000U
#define X286_PAGE_MASK   (X286_PAGE - 1U)
#define X286_ROUND_UP(x) (((x) + X286_PAGE_MASK) & ~X286_PAGE_MASK)


/* Bounds on the startup stack image so a hostile argv cannot run DGROUP out
 * of room before the program has drawn its first character. */
#define X286_MAX_ARGC    128
#define X286_MAX_ENVC    128
#define X286_STRING_CAP  0x2000U   /* 8 KiB of argv+envp text */

static int x286_debug_enabled(void) {
    return cmdline_debug_enabled("perso:x286:xout");
}

/*
 * argv/envp arrive as pointers into the *outgoing* address space, and the
 * first thing this loader does is replace it -- so snapshot both vectors into
 * kernel memory before pmap_create(), and build the new stack from the copy.
 */
static int x286_is_user_ptr(const void *p) {
    return (uintptr_t)p < KERNEL_VA_START;
}

static int x286_capture_ptr(char *const array[], int index, char **out) {
    return exec_vec_ptr(array, index, out);
}

static void x286_free_vector(char **vec) {
    size_t i;

    if (!vec) {
        return;
    }
    for (i = 0; vec[i]; i++) {
        kfree(vec[i], strlen(vec[i]) + 1U);
    }
    kfree(vec, (i + 1U) * sizeof(char *));
}

static int x286_dup_vector(char *const src[], char ***out) {
    char **dst;
    int count = 0;
    int i;

    *out = NULL;
    while (src && count < X286_MAX_ARGC) {
        char *item;

        if (x286_capture_ptr(src, count, &item) != 0) {
            return -EFAULT;
        }
        if (!item) {
            break;
        }
        count++;
    }
    if (count >= X286_MAX_ARGC) {
        return -E2BIG;
    }

    dst = kmalloc(((size_t)count + 1U) * sizeof(char *));
    if (!dst) {
        return -ENOMEM;
    }
    memset(dst, 0, ((size_t)count + 1U) * sizeof(char *));

    for (i = 0; i < count; i++) {
        char *item;
        size_t len = 0;
        char *copy;

        if (x286_capture_ptr(src, i, &item) != 0 || !item) {
            x286_free_vector(dst);
            return -EFAULT;
        }
        if (x286_is_user_ptr(item)) {
            if (copyinstr(item, NULL, X286_STRING_CAP, &len) != 0) {
                x286_free_vector(dst);
                return -E2BIG;
            }
        } else {
            len = strlen(item) + 1U;
            if (len > X286_STRING_CAP) {
                x286_free_vector(dst);
                return -E2BIG;
            }
        }
        copy = kmalloc(len);
        if (!copy) {
            x286_free_vector(dst);
            return -ENOMEM;
        }
        if (x286_is_user_ptr(item)) {
            if (copyinstr(item, copy, len, NULL) != 0) {
                kfree(copy, len);
                x286_free_vector(dst);
                return -EFAULT;
            }
        } else {
            memcpy(copy, item, len);
        }
        dst[i] = copy;
    }
    *out = dst;
    return 0;
}

static int x286_fail(int fd, int err, const char *msg) {
    if (msg) {
        kprint(msg);
        kprint("\n");
    }
    if (fd >= 0) {
        kern_close(fd);
    }
    return err;
}

/* Same, for the failures that happen once argv/envp have been snapshotted. */
static int x286_fail_v(int fd, char **kargv, char **kenvp, int err,
                       const char *msg) {
    x286_free_vector(kargv);
    x286_free_vector(kenvp);
    return x286_fail(fd, err, msg);
}

/*
 * Map one 64 KiB segment window as anonymous zero-fill and eagerly back the
 * sub-ranges we are about to write through the user VA (the on-disk image at
 * the bottom, the stack at the top).  Everything untouched demand-zeros, so
 * a segment whose image is 200 bytes costs one page, not sixteen.
 */
static int x286_map_window(vm_map_t *map, pmap_t pmap, uint32_t base,
                           uint8_t prot, vm_object_t **obj_out) {
    vm_object_t *obj;

    (void)pmap;
    *obj_out = NULL;
    obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, XOUT286_WINDOW_SIZE);
    if (!obj) {
        return -ENOMEM;
    }
    if (vm_map_insert(map, obj, 0, base, base + XOUT286_WINDOW_SIZE,
                      prot, prot, VM_INHERIT_COPY) != 0) {
        vm_object_deallocate(obj);
        return -ENOMEM;
    }
    *obj_out = obj;
    return 0;
}

static int x286_populate(pmap_t pmap, vm_object_t *obj, uint32_t base,
                         uint32_t off, uint32_t length, uint8_t prot) {
    uint32_t o;

    if (!obj || length == 0) {
        return 0;
    }
    off &= ~X286_PAGE_MASK;
    if (off >= XOUT286_WINDOW_SIZE) {
        return -EINVAL;
    }
    if (length > XOUT286_WINDOW_SIZE - off) {
        length = XOUT286_WINDOW_SIZE - off;
    }
    for (o = off; o < off + X286_ROUND_UP(length); o += X286_PAGE) {
        vm_page_t *page = vm_page_alloc(obj, (uint64_t)(o >> 12), 0);
        void *page_kva;

        if (!page) {
            return -ENOMEM;
        }
        vm_object_add_page(obj, page);
        /* The page is zeroed and filled through the kernel direct map,
         * whose extent is the architecture's (<machine/pmm.h>). */
        if (!pmm_phys_is_direct_mapped(page->phys_addr)) {
            return -ENOMEM;
        }
        page_kva = P2V(page->phys_addr);
        memset(page_kva, 0, X286_PAGE);
        if (pmap_enter(pmap, base + o, page->phys_addr, prot, 0) < 0) {
            return -ENOMEM;
        }
    }
    return 0;
}

/*
 * A 16-bit descriptor: D/B clear, granularity clear, so `limit` is the last
 * valid byte offset and the segment can be at most 64 KiB.  `contents` 2 is
 * an execute/read code segment; 0 is a read/write expand-up data segment.
 */
static void x286_fill_descriptor(gdt_entry_t *entry, uint32_t base,
                                 uint32_t byte_size, int code) {
    struct user_desc info;

    memset(&info, 0, sizeof(info));
    if (byte_size == 0 || byte_size > XOUT286_WINDOW_SIZE) {
        byte_size = XOUT286_WINDOW_SIZE;
    }
    info.base_addr = base;
    info.limit = byte_size - 1U;
    info.limit_in_pages = 0;
    info.seg_32bit = 0;              /* 16-bit: this is the whole point */
    info.contents = code ? 2 : 0;
    info.read_exec_only = 0;
    info.seg_not_present = 0;
    info.useable = 1;

    fill_ldt_entry(entry, &info);
}

/*
 * Build the Xenix/286 startup stack at the top of DGROUP.  The layout is the
 * 16-bit form of the classic Unix one -- crt0's `start0` does nothing but
 * `sub %bp,%bp` before calling the C startup, which reads argc at [bp+4] and
 * takes &argv[0] as [bp+6]:
 *
 *   [strings ...]        (highest offsets, just under 0x10000)
 *   NULL
 *   envp[n-1] .. envp[0]
 *   NULL
 *   argv[argc-1] .. argv[0]
 *   argc                 <- initial SP
 *
 * The vectors are 16-bit offsets within DGROUP for a small/medium-data image,
 * and 32-bit far pointers -- offset then DS -- for a large-data one (XE_LDATA).
 * That is not a detail the C runtime papers over: a large-data crt0 walks the
 * vector with a stride of 4 and stops on a NULL *far* pointer, so handing it
 * near offsets makes it read each pair of offsets as one pointer and take the
 * second as a selector.  emacs died on exactly that -- `les bx,[bp+0xa]` with
 * 0x4d20, another string's offset, arriving where a selector belonged.
 *
 * argc stays a single word in both layouts.  Returns the initial SP.
 */
static int x286_build_stack(uint8_t *dgroup, char *const argv[],
                            char *const envp[], uint16_t ds_sel, int far_vec,
                            uint16_t *sp_out) {
    int argc = 0, envc = 0, i;
    uint32_t strtop = XOUT286_WINDOW_SIZE;
    uint32_t strfloor = XOUT286_WINDOW_SIZE - X286_STRING_CAP;
    uint32_t words, vec_off;
    uint16_t argv_off[X286_MAX_ARGC];
    uint16_t envp_off[X286_MAX_ENVC];
    uint16_t *vec;
    uint32_t w = 0;

    while (argc < X286_MAX_ARGC && argv && argv[argc]) {
        argc++;
    }
    while (envc < X286_MAX_ENVC && envp && envp[envc]) {
        envc++;
    }

    for (i = argc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(argv[i]) + 1U;
        if (len > strtop || strtop - len < strfloor) {
            return -E2BIG;
        }
        strtop -= len;
        memcpy(dgroup + strtop, argv[i], len);
        argv_off[i] = (uint16_t)strtop;
    }
    for (i = envc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(envp[i]) + 1U;
        if (len > strtop || strtop - len < strfloor) {
            return -E2BIG;
        }
        strtop -= len;
        memcpy(dgroup + strtop, envp[i], len);
        envp_off[i] = (uint16_t)strtop;
    }

    strtop &= ~1U;   /* the 286 wants word alignment, not dword */

    /* argc, then argv[] + NULL + envp[] + NULL at one or two words each. */
    {
        uint32_t slot = far_vec ? 2U : 1U;

        words = 1U + ((uint32_t)argc + 1U + (uint32_t)envc + 1U) * slot;
    }
    if (words * 2U > strtop || strtop - words * 2U < strfloor) {
        return -E2BIG;
    }
    vec_off = strtop - words * 2U;
    vec = (uint16_t *)(dgroup + vec_off);

    vec[w++] = (uint16_t)argc;
    for (i = 0; i < argc; i++) {
        vec[w++] = argv_off[i];
        if (far_vec) {
            vec[w++] = ds_sel;
        }
    }
    vec[w++] = 0;                 /* end of argv */
    if (far_vec) {
        vec[w++] = 0;             /* ...a full NULL far pointer */
    }
    for (i = 0; i < envc; i++) {
        vec[w++] = envp_off[i];
        if (far_vec) {
            vec[w++] = ds_sel;
        }
    }
    vec[w++] = 0;                 /* end of envp */
    if (far_vec) {
        vec[w++] = 0;
    }

    *sp_out = (uint16_t)vec_off;
    return 0;
}

/*
 * Which x_cpu values this loader claims.
 *
 * A real Xenix/286 ran Xenix/86 binaries, so this does too -- the 8086
 * instruction set is a strict subset of the 286's, and an 8086 x.out is not a
 * real-mode image: it carries the same protected-mode LDT selectors (0x3f
 * text / 0x47 data), the same x_renv, and the same segment table as a 286
 * one.  The only thing the byte records is which instruction subset the
 * compiler targeted, which a 286 executes natively.
 *
 * This matters because SCO shipped the Development System as the *x86*
 * Development System: cc, masm, ld and cpp are all x_cpu 0x04, as is bc, and
 * rejecting them left the compiler on the disk but unrunnable.
 *
 * 80186 is deliberately NOT accepted: it is equally a subset, but nothing on
 * the media is built for it, so claiming it would be untested.
 */
static int x286_cpu_supported(uint8_t x_cpu) {
    switch (x_cpu & XC_CPU_MASK) {
    case XC_80286:
    case XC_8086:
        return 1;
    default:
        return 0;
    }
}

static int x286_check_file(const char *path, const char *header, size_t len) {
    const struct xexec *hdr = (const struct xexec *)header;

    (void)path;
    if (!header || len < sizeof(struct xexec)) {
        return -ENOEXEC;
    }
    if (hdr->x_magic != XOUT_MAGIC) {
        return -ENOEXEC;
    }
    if (!x286_cpu_supported(hdr->x_cpu)) {
        return -ENOEXEC;
    }
    if (!(hdr->x_renv & XE_SEG) || !(hdr->x_renv & XE_EXEC)) {
        return -ENOEXEC;
    }
    return 0;
}

static int x286_load(int fd, const char *path, char *const argv[],
                     char *const envp[]) {
    struct xexec hdr;
    struct xext ext;
    uint8_t segtab[XOUT_SEG_STRIDE * XOUT286_MAX_SEGS];
    gdt_entry_t entries[XOUT286_MAX_SEGS];
    unsigned int nsegs, s;
    unsigned int max_ldt_index = 0;
    unsigned int dgroup_index = 0;
    pmap_t pmap;
    vm_map_t *map;
    uint16_t cs_sel = 0, ds_sel = 0;
    uint32_t dgroup_base = 0, dgroup_break = 0;
    uint16_t user_sp = 0;
    int have_dgroup = 0;
    char **kargv = NULL;
    char **kenvp = NULL;
    int rc;

    /* --- headers --- */
    kern_lseek(fd, 0, 0);
    if (kern_read(fd, (char *)&hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        return x286_fail(fd, -ENOEXEC, "xout286: short read on header");
    }
    if (hdr.x_magic != XOUT_MAGIC || !x286_cpu_supported(hdr.x_cpu)) {
        return x286_fail(fd, -ENOEXEC, "xout286: not an 8086/80286 x.out");
    }
    if (hdr.x_ext < sizeof(struct xext)) {
        return x286_fail(fd, -ENOEXEC, "xout286: missing extension header");
    }
    if (kern_read(fd, (char *)&ext, sizeof(ext)) != (int)sizeof(ext)) {
        return x286_fail(fd, -ENOEXEC, "xout286: short read on extension");
    }
    if (ext.xe_segpos <= 0 || ext.xe_segsize <= 0 ||
        (unsigned int)ext.xe_segsize > sizeof(segtab) ||
        ((unsigned int)ext.xe_segsize % XOUT_SEG_STRIDE) != 0) {
        return x286_fail(fd, -ENOEXEC, "xout286: invalid segment table");
    }

    nsegs = (unsigned int)ext.xe_segsize / XOUT_SEG_STRIDE;
    if (nsegs == 0) {
        return x286_fail(fd, -ENOEXEC, "xout286: empty segment table");
    }

    kern_lseek(fd, ext.xe_segpos, 0);
    if (kern_read(fd, (char *)segtab, (int)ext.xe_segsize) != ext.xe_segsize) {
        return x286_fail(fd, -EIO, "xout286: short read on segment table");
    }

    if (x286_debug_enabled()) {
        char b[144];
        snprintf(b, sizeof(b),
                 "xout286: %s cpu=%02x renv=%04x eseg=%04x nsegs=%u entry=%04x\n",
                 path ? path : "?", hdr.x_cpu, hdr.x_renv,
                 ext.xe_eseg, nsegs, (unsigned int)hdr.x_entry);
        kprint(b);
    }

    /* Validate every selector before we tear down the old address space:
     * once pmap_create() runs there is no going back to the caller's image. */
    for (s = 0; s < nsegs; s++) {
        const struct xseg *seg =
            (const struct xseg *)(segtab + s * XOUT_SEG_STRIDE);
        unsigned int idx = XOUT_SEL_INDEX(seg->xs_seg);

        if (seg->xs_type != XS_TEXT && seg->xs_type != XS_DATA) {
            continue;   /* symbol / relocation segments are not loaded */
        }
        if ((seg->xs_seg & 0x04U) == 0U) {
            return x286_fail(fd, -ENOEXEC, "xout286: non-LDT selector");
        }
        if (idx == 0 || idx >= XOUT286_MAX_SEGS) {
            return x286_fail(fd, -ENOEXEC, "xout286: selector out of range");
        }
        if ((uint32_t)seg->xs_psize > XOUT286_WINDOW_SIZE ||
            (uint32_t)seg->xs_vsize > XOUT286_WINDOW_SIZE) {
            return x286_fail(fd, -ENOEXEC, "xout286: segment exceeds 64 KiB");
        }
        if (idx > max_ldt_index) {
            max_ldt_index = idx;
        }
    }
    if (max_ldt_index == 0) {
        return x286_fail(fd, -ENOEXEC, "xout286: no loadable segments");
    }

    /*
     * An impure image -- XE_SEP clear, "combined I & D" -- has no text segment
     * at all.  Code and data share one segment, and the program reaches them
     * through two selectors aliased onto it: xe_eseg for CS, the data
     * segment's own selector for DS/ES/SS.  So the segment table holds a lone
     * XS_DATA entry (0x47 in practice) while xe_eseg names 0x3f, an index that
     * appears nowhere in the table.
     *
     * This is what `cc` produces without -i, which is its default, so it is
     * the shape of every program built on the target unless the user knows to
     * ask otherwise.  Reserve the entry selector's LDT slot here, while
     * failing is still free -- past pmap_create() there is no caller left to
     * return to -- and alias the descriptor onto DGROUP once its base is
     * known.
     */
    if (!(hdr.x_renv & XE_SEP)) {
        unsigned int eidx = XOUT_SEL_INDEX(ext.xe_eseg);

        if ((ext.xe_eseg & 0x04U) == 0U) {
            return x286_fail(fd, -ENOEXEC,
                             "xout286: impure image with non-LDT entry selector");
        }
        if (eidx == 0 || eidx >= XOUT286_MAX_SEGS) {
            return x286_fail(fd, -ENOEXEC,
                             "xout286: impure image entry selector out of range");
        }
        if (eidx > max_ldt_index) {
            max_ldt_index = eidx;
        }
    }

    /* Snapshot argv/envp while the caller's address space still exists. */
    rc = x286_dup_vector(argv, &kargv);
    if (rc == 0) {
        rc = x286_dup_vector(envp, &kenvp);
        if (rc != 0) {
            x286_free_vector(kargv);
        }
    }
    if (rc != 0) {
        return x286_fail(fd, rc, "xout286: cannot capture argv/envp");
    }

    /* --- address space --- */
    pmap = pmap_create();
    if (!pmap) {
        return x286_fail_v(fd, kargv, kenvp, -ENOMEM, "xout286: pmap_create failed");
    }
    current_process->pmap = (struct pmap *)pmap;
    pmap_activate(pmap);
    map = vm_map_create(pmap, 0x10000, USER32_VA_END);
    if (!map) {
        return x286_fail_v(fd, kargv, kenvp, -ENOMEM, "xout286: vm_map_create failed");
    }

    memset(entries, 0, sizeof(entries));

    /* --- load each segment into its own 64 KiB window --- */
    for (s = 0; s < nsegs; s++) {
        const struct xseg *seg =
            (const struct xseg *)(segtab + s * XOUT_SEG_STRIDE);
        unsigned int idx = XOUT_SEL_INDEX(seg->xs_seg);
        uint32_t psize = (uint32_t)seg->xs_psize;
        uint32_t vsize = (uint32_t)seg->xs_vsize;
        uint32_t base = xout286_window_base(idx);
        uint32_t limit_size;
        uint8_t prot;
        vm_object_t *obj = NULL;
        int is_code;

        if (seg->xs_type == XS_TEXT) {
            is_code = 1;
            /* Xenix marked text pure, but the image is private here and the
             * personality has to be able to write a breakpoint eventually. */
            prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC;
            limit_size = psize > vsize ? psize : vsize;
        } else if (seg->xs_type == XS_DATA) {
            is_code = 0;
            prot = VM_PROT_READ | VM_PROT_WRITE;
            /* DGROUP gets the whole 64 KiB so break and stack have room;
             * far data segments are sized to their contents and grow only
             * when brkctl(BR_ARGSEG) raises the limit. */
            limit_size = have_dgroup ? (vsize > psize ? vsize : psize)
                                     : XOUT286_WINDOW_SIZE;
        } else {
            continue;
        }

        rc = x286_map_window(map, pmap, base, prot, &obj);
        if (rc != 0) {
            return x286_fail_v(fd, kargv, kenvp, rc, "xout286: failed to map segment window");
        }
        if (psize > 0) {
            rc = x286_populate(pmap, obj, base, 0, psize, prot);
            if (rc != 0) {
                return x286_fail_v(fd, kargv, kenvp, rc, "xout286: failed to back image");
            }
        }

        if (seg->xs_type == XS_DATA && !have_dgroup) {
            /* Back the stack region eagerly: we write the argv image into it
             * through the user VA before the process ever runs. */
            rc = x286_populate(pmap, obj, base,
                               XOUT286_WINDOW_SIZE - X286_STRING_CAP -
                                   X286_PAGE,
                               X286_STRING_CAP + X286_PAGE, prot);
            if (rc != 0) {
                return x286_fail_v(fd, kargv, kenvp, rc, "xout286: failed to back stack");
            }
        }

        if (psize > 0 && seg->xs_filpos > 0) {
            int got;

            kern_lseek(fd, seg->xs_filpos, 0);
            got = kern_read(fd, (void *)(uintptr_t)base, (int)psize);
            if (got < 0) {
                return x286_fail_v(fd, kargv, kenvp, -EIO, "xout286: read error on segment");
            }
            if ((uint32_t)got < psize && x286_debug_enabled()) {
                char b[96];
                snprintf(b, sizeof(b),
                         "xout286: sel=%04x short read %d/%u\n",
                         seg->xs_seg, got, psize);
                kprint(b);
            }
        }

        x286_fill_descriptor(&entries[idx], base, limit_size, is_code);

        if (seg->xs_type == XS_DATA && !have_dgroup) {
            have_dgroup = 1;
            dgroup_index = idx;
            dgroup_base = base;
            ds_sel = seg->xs_seg;
            /* The initial break sits just past data+bss, word-aligned. */
            dgroup_break = ((vsize > psize ? vsize : psize) + 1U) & ~1U;
        }
        if (seg->xs_type == XS_TEXT && seg->xs_seg == ext.xe_eseg) {
            cs_sel = seg->xs_seg;
        }
    }

    if (!have_dgroup) {
        return x286_fail_v(fd, kargv, kenvp, -ENOEXEC, "xout286: no DGROUP data segment");
    }

    /* Impure image: give xe_eseg a code descriptor over the very same bytes
     * DGROUP already covers.  Executing from a data descriptor is not
     * possible, so the alias is what makes the program runnable at all -- and
     * because both descriptors share a base, an offset means the same thing
     * whether the program computed it as code or as data, which is precisely
     * what "combined I & D" promises.  Full window: the segment holds text,
     * data, bss, the break and the stack, and the stack sits at the top. */
    if (cs_sel == 0 && !(hdr.x_renv & XE_SEP)) {
        unsigned int eidx = XOUT_SEL_INDEX(ext.xe_eseg);

        x286_fill_descriptor(&entries[eidx], dgroup_base,
                             XOUT286_WINDOW_SIZE, 1 /* code */);
        cs_sel = ext.xe_eseg;
        if (x286_debug_enabled()) {
            char b[96];

            snprintf(b, sizeof(b),
                     "xout286: impure image, cs=%04x aliased onto ds=%04x\n",
                     cs_sel, ds_sel);
            kprint(b);
        }
    }

    if (cs_sel == 0) {
        return x286_fail_v(fd, kargv, kenvp, -ENOEXEC, "xout286: entry segment not loaded");
    }
    if ((uint32_t)hdr.x_entry >= XOUT286_WINDOW_SIZE) {
        return x286_fail_v(fd, kargv, kenvp, -ENOEXEC, "xout286: entry offset out of segment");
    }

    /* --- install the LDT --- */
    if (ldt_replace_process(current_process, entries, max_ldt_index + 1U) != 0) {
        return x286_fail_v(fd, kargv, kenvp, -ENOMEM, "xout286: ldt_replace_process failed");
    }
    ldt_activate(current_process);

    /* --- process state --- */
    current_process->perso_id = PERS_XENIX;
    current_process->bitness = BITNESS_16;
    current_process->brk_start = dgroup_break;
    current_process->brk = dgroup_break;
    /* POSIX: exec resets caught signals to SIG_DFL.  Without this the image
     * inherits the *previous* program's handler addresses, which under this
     * personality are far pointers into an address space that no longer
     * exists. */
    proc_exec_reset_signals();
    /* And the new image starts with the FPU in its initial state. */
    fpu_thread_reset(current_thread);
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

    /* What ps and /proc/<pid>/cmdline report.  The process arrived here by
     * fork, so it still carries its parent's argv snapshot and the bounds
     * of its parent's argv region; left alone, a Xenix/286 program showed
     * its parent's command line.  The new argv is far pointers in DGROUP,
     * not a flat region procfs can read back, so drop the bounds and keep
     * the snapshot. */
    current_process->arg_start = 0;
    current_process->arg_end = 0;
    proc_capture_cmdline(current_process, kargv);

    /* --- startup stack, written through DGROUP's linear window --- */
    rc = x286_build_stack((uint8_t *)(uintptr_t)dgroup_base, kargv, kenvp,
                          ds_sel, (hdr.x_renv & XE_LDATA) ? 1 : 0,
                          &user_sp);
    x286_free_vector(kargv);
    x286_free_vector(kenvp);
    if (rc != 0) {
        return x286_fail(fd, rc, "xout286: argv/envp too large for DGROUP");
    }
    if ((uint32_t)user_sp <= dgroup_break) {
        return x286_fail(fd, -E2BIG, "xout286: stack collides with bss");
    }

    proc_close_cloexec(current_process);
    kern_close(fd);

    if (x286_debug_enabled()) {
        char b[144];
        snprintf(b, sizeof(b),
                 "xout286: enter cs=%04x:%04x ds=ss=%04x:%04x dgroup=slot%u "
                 "brk=%04x\n",
                 cs_sel, (unsigned int)hdr.x_entry, ds_sel, user_sp,
                 dgroup_index, dgroup_break);
        kprint(b);
    }

    /* Reassert pmap/LDT immediately before handoff. */
    pmap_activate((pmap_t)(uintptr_t)current_process->pmap);
    ldt_activate(current_process);

    jump_to_elks((uint32_t)hdr.x_entry, (uint32_t)user_sp, cs_sel, ds_sel,
                 ds_sel, ds_sel, 0);

    return 0;   /* not reached */
}

/* =====================================================================
 * One handler for every x.out.
 *
 * The 8086, 80286 and 80386 Xenix targets share one magic number; x_cpu
 * tells them apart, and decides which of the two loaders above takes the
 * image.  Both leave the process under the one Xenix personality
 * (PERS_XENIX), with its bitness saying which half of it applies.
 */
static int xout_any_check_file(const char *path, const char *header,
                               size_t len) {
    if (xout_check_file(path, header, len) == 0) {
        return 0;
    }
    return x286_check_file(path, header, len);
}

static int xout_any_load(int fd, const char *path, char *const argv[],
                         char *const envp[]) {
    struct xexec hdr;

    kern_lseek(fd, 0, 0);
    if (kern_read(fd, (char *)&hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        return xout_fail(fd, -ENOEXEC, "xout: short read on header");
    }
    /* Each loader rereads the header from the start of the file. */
    if (x286_cpu_supported(hdr.x_cpu)) {
        return x286_load(fd, path, argv, envp);
    }
    return xout_load(fd, path, argv, envp);
}

static struct exec_binary_handler xout_handler = {
    .name = "Xenix x.out",
    .check = xout_any_check_file,
    .load = xout_any_load,
    .next = NULL,
};

void xout_init_handler(void) {
    exec_register_handler(&xout_handler);
}
