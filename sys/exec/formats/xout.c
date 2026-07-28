/*
 * xout.c - Loader for Microsoft x.out segmented executables (Xenix/386).
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

#include <arch/i386/gdt.h>
#include <arch/i386/pmap.h>
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

/* Direct-map ceiling: pages above this cannot be touched through the kernel
 * direct map while we populate them (see elks_map_object_pages). */
#define XOUT_PHYS_LIMIT  0x3EC00000U

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
        if (page->phys_addr >= XOUT_PHYS_LIMIT) {
            return -ENOMEM;
        }
        /* Zero via the kernel direct map -- the user VA is not mapped yet. */
        page_kva = (void *)(uintptr_t)(page->phys_addr + 0xC0000000U);
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

    for (argc = 0; argc < 63 && argv && argv[argc]; argc++) {
        ;
    }
    for (envc = 0; envc < 63 && envp && envp[envc]; envc++) {
        ;
    }

    /* Copy strings into the top of the segment, recording their offsets. */
    for (i = argc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(argv[i]) + 1U;
        strtop -= len;
        memcpy(data_seg + strtop, argv[i], len);
        argv_off[i] = strtop;
    }
    for (i = envc - 1; i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(envp[i]) + 1U;
        strtop -= len;
        memcpy(data_seg + strtop, envp[i], len);
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
    map = vm_map_create(pmap, 0x10000, 0xC0000000U);
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
    arch_set_kernel_stack((uintptr_t)current_thread->kstack_top);

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

static struct exec_binary_handler xout_handler = {
    .name = "Xenix x.out",
    .check = xout_check_file,
    .load = xout_load,
    .next = NULL,
};

void xout_init_handler(void) {
    exec_register_handler(&xout_handler);
}
