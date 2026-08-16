/*
 * xout286.c - Loader for Microsoft x.out 80286 segmented executables.
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
 * data, bss, the malloc arena (grown via brkctl(2), see perso_sco_x286.c)
 * and the process stack at its top.  Xenix sized that segment to 64 KiB and
 * grew break and stack toward each other, and so do we.
 *
 * Execution begins in 16-bit mode; system calls trap out through `int $5`
 * and are emulated by the SCO-X/286 personality.
 */

#include <stdio.h>
#include <string.h>

#include <arch/i386/gdt.h>
#include <arch/i386/pmap.h>
#include <exec/formats/xout.h>
#include <exec/formats/xout286.h>
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

#define X286_PAGE        0x1000U
#define X286_PAGE_MASK   (X286_PAGE - 1U)
#define X286_ROUND_UP(x) (((x) + X286_PAGE_MASK) & ~X286_PAGE_MASK)

/* Pages above this cannot be reached through the kernel direct map while we
 * zero them at load time (mirrors the ELKS/xout loaders). */
#define X286_PHYS_LIMIT  0x3EC00000U

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
    return (uintptr_t)p < 0xC0000000U;
}

static int x286_capture_ptr(char *const array[], int index, char **out) {
    if (x286_is_user_ptr(array)) {
        return copyin(&array[index], out, sizeof(char *));
    }
    *out = array[index];
    return 0;
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
        if (page->phys_addr >= X286_PHYS_LIMIT) {
            return -ENOMEM;
        }
        page_kva = (void *)(uintptr_t)(page->phys_addr + 0xC0000000U);
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
 * All pointers are 16-bit offsets within DGROUP.  Returns the initial SP.
 */
static int x286_build_stack(uint8_t *dgroup, char *const argv[],
                            char *const envp[], uint16_t *sp_out) {
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

    /* argc + argv[] + NULL + envp[] + NULL, all 16-bit. */
    words = 1U + (uint32_t)argc + 1U + (uint32_t)envc + 1U;
    if (words * 2U > strtop || strtop - words * 2U < strfloor) {
        return -E2BIG;
    }
    vec_off = strtop - words * 2U;
    vec = (uint16_t *)(dgroup + vec_off);

    vec[w++] = (uint16_t)argc;
    for (i = 0; i < argc; i++) {
        vec[w++] = argv_off[i];
    }
    vec[w++] = 0;
    for (i = 0; i < envc; i++) {
        vec[w++] = envp_off[i];
    }
    vec[w++] = 0;

    *sp_out = (uint16_t)vec_off;
    return 0;
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
    if ((hdr->x_cpu & XC_CPU_MASK) != XC_80286) {
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
    if (hdr.x_magic != XOUT_MAGIC ||
        (hdr.x_cpu & XC_CPU_MASK) != XC_80286) {
        return x286_fail(fd, -ENOEXEC, "xout286: not an 80286 x.out");
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
    map = vm_map_create(pmap, 0x10000, 0xC0000000U);
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
    current_process->perso_id = PERS_SCO_X286;
    current_process->bitness = BITNESS_16;
    current_process->brk_start = dgroup_break;
    current_process->brk = dgroup_break;
    /* POSIX: exec resets caught signals to SIG_DFL.  Without this the image
     * inherits the *previous* program's handler addresses, which under this
     * personality are far pointers into an address space that no longer
     * exists. */
    proc_exec_reset_signals();
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

    /* --- startup stack, written through DGROUP's linear window --- */
    rc = x286_build_stack((uint8_t *)(uintptr_t)dgroup_base, kargv, kenvp,
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

static struct exec_binary_handler xout286_handler = {
    .name = "SCO-X/286 x.out",
    .check = x286_check_file,
    .load = x286_load,
    .next = NULL,
};

void xout286_init_handler(void) {
    exec_register_handler(&xout286_handler);
}
