#include <exec/formats/coff.h>

#ifndef HOST_TEST
#include <stdio.h>
#include <machine/fpu.h>
#include <machine/gdt.h>
#include <machine/pmap.h>
#include <exec/formats/aout.h>
#include <exec/perso/personality.h>
#include <kern/arch.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <pm/pm.h>
#include <sys/errno.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/sysinfo.h>
#include <vm/vm_kmem.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#endif
#include <machine/vmparam.h>

#include <string.h>

/*
 * Hard cap on per-segment size.  COFF stores sizes as int32_t; we further
 * reject anything that would overflow user-space layout calculations.  The
 * cap is intentionally well below 2 GiB so that text+data+bss arithmetic
 * cannot wrap a 32-bit address.
 */
#define COFF_MAX_SEGMENT_BYTES (1U << 30)  /* 1 GiB */

int coff_validate_filehdr(const coff_filehdr_t *fh, uint32_t file_size) {
    if (!fh) return -1;
    if (fh->f_magic != COFF_MAGIC_I386) return -1;
    if (fh->f_nscns == 0 || fh->f_nscns > 256) return -1;

    /* The optional header (if present) sits immediately after the file
     * header; the section table follows.  Bound both against file_size. */
    if (fh->f_opthdr > 4096) return -1;  /* sane upper bound */
    uint32_t scn_offset = (uint32_t)sizeof(coff_filehdr_t) + (uint32_t)fh->f_opthdr;
    if (scn_offset > file_size) return -1;

    uint32_t scn_table_bytes = (uint32_t)fh->f_nscns * (uint32_t)sizeof(coff_scnhdr_t);
    if (scn_offset + scn_table_bytes < scn_offset) return -1;  /* overflow */
    if (scn_offset + scn_table_bytes > file_size) return -1;

    return 0;
}

int coff_validate_aouthdr(const coff_aouthdr_t *opt) {
    if (!opt) return -1;

    /* REQ-05-0407, REQ-05-0408: only ZMAGIC (demand-paged) is accepted. */
    if (opt->magic != AOUT_ZMAGIC) return -1;

    /* REQ-05-0413: sizes must be non-negative and bounded.  Negative values
     * are nonsensical for an executable image; very large values are likely
     * corrupted headers and would overflow address arithmetic below. */
    if (opt->tsize < 0 || (uint32_t)opt->tsize > COFF_MAX_SEGMENT_BYTES) return -1;
    if (opt->dsize < 0 || (uint32_t)opt->dsize > COFF_MAX_SEGMENT_BYTES) return -1;
    if (opt->bsize < 0 || (uint32_t)opt->bsize > COFF_MAX_SEGMENT_BYTES) return -1;

    /* tsize is the text section's size to the byte.  What keeps text and
     * data in disjoint page frames is the linker's layout -- data starts
     * 4 MiB up, at the same offset within its page that it has in the file
     * -- and not any rounding of the sizes. */

    /* REQ-05-0420: text_start must be strictly below data_start when both
     * are present, and text+tsize must fit before data_start.  When the
     * binary is BSS-only (tsize == 0) we relax the ordering requirement. */
    if (opt->tsize > 0) {
        uint32_t text_end = (uint32_t)opt->text_start + (uint32_t)opt->tsize;
        if (text_end < (uint32_t)opt->text_start) return -1;  /* overflow */

        if (opt->dsize > 0 || opt->bsize > 0) {
            if ((uint32_t)opt->data_start < text_end) return -1;
        }

        /* REQ-05-0416: entry must fall within the text segment. */
        if ((uint32_t)opt->entry < (uint32_t)opt->text_start ||
            (uint32_t)opt->entry >= text_end) {
            return -1;
        }
    } else if (opt->dsize > 0 || opt->bsize > 0) {
        /* BSS-only / data-only image: there's no text to anchor entry in. */
        if (opt->entry != 0) return -1;
    }

    return 0;
}

int coff_apply_relocation(uint8_t *section_data, uint32_t section_va,
                          uint32_t section_size, const coff_reloc_t *r,
                          uint32_t symbol_value) {
    if (!section_data || !r) return -1;
    if (section_size < 4) return -1;

    /* The relocation site lives at r->r_vaddr in user-space terms; translate
     * into an offset within the section's raw bytes.  Reject OOB sites and
     * the case where the 4-byte fixup would run past the end of section. */
    if ((uint32_t)r->r_vaddr < section_va) return -1;
    uint32_t off = (uint32_t)r->r_vaddr - section_va;
    if (off > section_size - 4U) return -1;

    uint8_t *site = section_data + off;

    /* Read the existing 32-bit value (the addend the linker stored).  COFF
     * is little-endian; on i386 we could just cast, but the test runs on a
     * potentially differently-aligned host so use byte-wise I/O. */
    uint32_t addend = (uint32_t)site[0]
                    | ((uint32_t)site[1] << 8)
                    | ((uint32_t)site[2] << 16)
                    | ((uint32_t)site[3] << 24);

    uint32_t result;
    switch (r->r_type) {
    case R_DIR32:
        /* Absolute 32-bit: addend + symbol_value. */
        result = addend + symbol_value;
        break;
    case R_PCRLONG:
        /* 32-bit PC-relative: symbol - (site + 4) + addend.  The "+4" is
         * because PC-relative on i386 reckons from the byte after the
         * relocated displacement, not from the displacement itself. */
        result = symbol_value - ((uint32_t)r->r_vaddr + 4U) + addend;
        break;
    default:
        return -1;
    }

    site[0] = (uint8_t)(result & 0xFF);
    site[1] = (uint8_t)((result >> 8) & 0xFF);
    site[2] = (uint8_t)((result >> 16) & 0xFF);
    site[3] = (uint8_t)((result >> 24) & 0xFF);
    return 0;
}

int coff_apply_relocations(uint8_t *section_data, uint32_t section_va,
                           uint32_t section_size,
                           const coff_reloc_t *relocs, uint32_t nrelocs,
                           coff_symbol_resolver_t resolve, void *ctx) {
    if (!section_data || !resolve) return -1;
    if (nrelocs > 0 && !relocs) return -1;

    for (uint32_t i = 0; i < nrelocs; i++) {
        uint32_t symval = resolve(relocs[i].r_symndx, ctx);
        if (coff_apply_relocation(section_data, section_va, section_size,
                                  &relocs[i], symval) != 0) {
            return -1;
        }
    }
    return 0;
}

#ifndef HOST_TEST
/*
 * ---- the executable handler ------------------------------------------
 *
 * A System V/386 executable is a ZMAGIC COFF file laid out so that it can
 * be paged straight from disk: .text is at the address that is its offset
 * in the file (0xd0, just past the headers), .data is 4 MiB up at the same
 * offset within its page that it has in the file, and .bss follows it.
 * Nothing is relocated.  The stack is at the top of user space and starts
 * with argc, argv[], NULL, envp[], NULL.
 *
 * Most programs are linked against a static shared library, /shlib/libc_s:
 * a COFF file of its own (magic 0443) whose sections sit at fixed
 * addresses, 0xa0000000 and up.  A program names the libraries it needs in
 * a .lib section, and holds their address ranges with NOLOAD sections that
 * are not to be loaded from it.  Each library is mapped privately -- its
 * data is per-process and its text, being small, is not worth sharing.
 */
#define COFF_MAX_SECTIONS 32
#define COFF_MAX_LIBS     8
#define COFF_LIB_PATH_MAX 128

struct coff_image {
    coff_filehdr_t fh;
    coff_aouthdr_t opt;
    coff_scnhdr_t  scn[COFF_MAX_SECTIONS];
};

static int coff_debug_enabled(void) {
    return cmdline_debug_enabled("exec:coff");
}

static int coff_fail(int fd, int err, const char *msg) {
    if (msg) {
        kprint(msg);
        kprint("\n");
    }
    if (fd >= 0) {
        kern_close(fd);
    }
    return err;
}

/* Read the headers of the COFF file open on `fd`. */
static int coff_read_image(int fd, struct coff_image *img) {
    int bytes;

    kern_lseek(fd, 0, 0);
    if (kern_read(fd, (char *)&img->fh, sizeof(img->fh)) != (int)sizeof(img->fh) ||
        img->fh.f_magic != COFF_MAGIC_I386 ||
        img->fh.f_opthdr < sizeof(img->opt) ||
        img->fh.f_nscns == 0 || img->fh.f_nscns > COFF_MAX_SECTIONS) {
        return -ENOEXEC;
    }
    if (kern_read(fd, (char *)&img->opt, sizeof(img->opt)) != (int)sizeof(img->opt)) {
        return -ENOEXEC;
    }
    kern_lseek(fd, (off_t)(sizeof(img->fh) + img->fh.f_opthdr), 0);
    bytes = (int)(img->fh.f_nscns * sizeof(coff_scnhdr_t));
    if (kern_read(fd, (char *)img->scn, bytes) != bytes) {
        return -ENOEXEC;
    }
    return 0;
}

/*
 * Map the loadable sections of `img` from `fd`: text, and data with the
 * bss after it as one region.  *end_out is the end of the bss.
 */
static int coff_map_sections(pmap_t pmap, vm_map_t *map, int fd,
                             const struct coff_image *img, uint32_t *end_out) {
    const coff_scnhdr_t *data = NULL, *bss = NULL;
    uint32_t end = 0;
    int i, rc;

    for (i = 0; i < img->fh.f_nscns; i++) {
        const coff_scnhdr_t *s = &img->scn[i];
        uint32_t va = (uint32_t)s->s_vaddr;
        uint32_t size = (uint32_t)s->s_size;

        if (size == 0 || (s->s_flags & (STYP_NOLOAD | STYP_DSECT | STYP_LIB))) {
            continue;
        }
        if (va >= USER32_VA_END || size > USER32_VA_END - va) {
            return -ENOEXEC;
        }
        if (s->s_flags & STYP_TEXT) {
            /* Writable as mapped: the image is read in through it. */
            rc = aout_map_region(pmap, map, va, size, size,
                                 VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC,
                                 fd, (uint32_t)s->s_scnptr);
            if (rc != 0) {
                return rc;
            }
        } else if ((s->s_flags & STYP_DATA) && !data) {
            data = s;
        } else if ((s->s_flags & STYP_BSS) && !bss) {
            bss = s;
        }
    }
    if (data || bss) {
        uint32_t va = (uint32_t)(data ? data->s_vaddr : bss->s_vaddr);
        uint32_t filesz = data ? (uint32_t)data->s_size : 0;

        end = va + filesz;
        if (bss && (uint32_t)bss->s_vaddr + (uint32_t)bss->s_size > end) {
            end = (uint32_t)bss->s_vaddr + (uint32_t)bss->s_size;
        }
        rc = aout_map_region(pmap, map, va, filesz, end - va,
                             VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC,
                             fd, data ? (uint32_t)data->s_scnptr : 0);
        if (rc != 0) {
            return rc;
        }
    }
    if (end_out) {
        *end_out = end;
    }
    return 0;
}

/* The paths in the .lib sections of `img`, read from `fd`. */
static int coff_read_libs(int fd, const struct coff_image *img,
                          char libs[][COFF_LIB_PATH_MAX], int *count_out) {
    int i, n = 0;

    for (i = 0; i < img->fh.f_nscns; i++) {
        const coff_scnhdr_t *s = &img->scn[i];
        uint32_t size = (uint32_t)s->s_size;
        uint32_t pos = 0;
        char *buf;

        if (!(s->s_flags & STYP_LIB) || size == 0) {
            continue;
        }
        if (size > 4096U) {
            return -ENOEXEC;
        }
        buf = kmalloc(size);
        if (!buf) {
            return -ENOMEM;
        }
        kern_lseek(fd, (off_t)s->s_scnptr, 0);
        if (kern_read(fd, buf, (int)size) != (int)size) {
            kfree(buf, size);
            return -ENOEXEC;
        }
        while (pos + 8U <= size) {
            uint32_t entry, path;

            memcpy(&entry, buf + pos, 4);       /* both counted in longs */
            memcpy(&path, buf + pos + 4, 4);
            if (entry < 2U || entry > (size - pos) / 4U || path >= entry) {
                break;
            }
            if (n < COFF_MAX_LIBS) {
                uint32_t len = (entry - path) * 4U;

                if (len >= COFF_LIB_PATH_MAX) {
                    len = COFF_LIB_PATH_MAX - 1U;
                }
                memcpy(libs[n], buf + pos + path * 4U, len);
                libs[n][len] = '\0';
                n++;
            }
            pos += entry * 4U;
        }
        kfree(buf, size);
    }
    *count_out = n;
    return 0;
}

static int coff_check_file(const char *path, const char *header, size_t len) {
    const coff_filehdr_t *fh = (const coff_filehdr_t *)(const void *)header;
    const coff_aouthdr_t *opt;

    (void)path;
    if (!header || len < sizeof(*fh) + sizeof(*opt)) {
        return -ENOEXEC;
    }
    opt = (const coff_aouthdr_t *)(const void *)(header + sizeof(*fh));
    if (fh->f_magic != COFF_MAGIC_I386 || !(fh->f_flags & COFF_F_EXEC) ||
        fh->f_opthdr < sizeof(*opt) || opt->magic != AOUT_ZMAGIC) {
        return -ENOEXEC;
    }
    return 0;
}

static int coff_load(int fd, const char *path, char *const argv[],
                     char *const envp[]) {
    struct coff_image *img;
    char (*libs)[COFF_LIB_PATH_MAX];
    char **kargv = NULL, **kenvp = NULL;
    int argc = 0, envc = 0, nlibs = 0;
    uint32_t brk = 0, sp = 0, entry;
    pmap_t pmap;
    vm_map_t *map;
    int rc, i;

    img = kmalloc(sizeof(*img));
    libs = kmalloc(COFF_MAX_LIBS * COFF_LIB_PATH_MAX);
    if (!img || !libs) {
        rc = -ENOMEM;
        goto fail;
    }
    rc = coff_read_image(fd, img);
    if (rc == 0 && coff_validate_aouthdr(&img->opt) != 0) {
        rc = -ENOEXEC;
    }
    if (rc == 0) {
        rc = coff_read_libs(fd, img, libs, &nlibs);
    }
    if (rc != 0) {
        goto fail;
    }
    entry = (uint32_t)img->opt.entry;

    /* argv and envp point into the address space about to be replaced. */
    rc = aout_dup_vector(argv, &kargv, &argc);
    if (rc == 0) {
        rc = aout_dup_vector(envp, &kenvp, &envc);
    }
    if (rc != 0) {
        goto fail;
    }

    pmap = pmap_create();
    if (!pmap) {
        rc = -ENOMEM;
        goto fail;
    }
    current_process->pmap = (struct pmap *)pmap;
    pmap_activate(pmap);
    /* Text starts in the first page, so the map does too. */
    map = vm_map_create(pmap, 0, USER32_VA_END);
    if (!map) {
        rc = -ENOMEM;
        goto fail;
    }

    rc = coff_map_sections(pmap, map, fd, img, &brk);
    if (rc != 0) {
        goto fail;
    }

    /* The process is SVR3 from here, so that the libraries are looked for
     * under the personality's root. */
    current_process->perso_id = PERS_SVR3;
    current_process->bitness = BITNESS_32;
    for (i = 0; i < nlibs; i++) {
        int lfd = kern_open(libs[i], O_RDONLY, 0);

        if (coff_debug_enabled()) {
            kprint("COFF: shared library ");
            kprint(libs[i]);
            kprint("\n");
        }
        if (lfd < 0) {
            kprint("COFF: cannot open shared library ");
            kprint(libs[i]);
            kprint("\n");
            rc = -ENOENT;
            goto fail;
        }
        rc = coff_read_image(lfd, img);
        if (rc == 0 && img->opt.magic != AOUT_LIBMAGIC) {
            rc = -ENOEXEC;
        }
        if (rc == 0) {
            rc = coff_map_sections(pmap, map, lfd, img, NULL);
        }
        kern_close(lfd);
        if (rc != 0) {
            kprint("COFF: bad shared library\n");
            goto fail;
        }
    }

    /* Caught signals revert to their defaults across exec, and the new
     * image starts with the FPU in its initial state. */
    proc_exec_reset_signals();
    fpu_thread_reset(current_thread);

    /* Pointers into the first page are good ones here. */
    current_process->low_va_valid = 1;
    /* Not rounded: libc counts the break up from its own `end`. */
    current_process->brk_start = brk;
    current_process->brk = brk;
    {
        const char *name = path ? path : "";
        const char *p;

        for (p = name; *p; p++) {
            if (*p == '/') {
                name = p + 1;
            }
        }
        strlcpy(current_process->comm, name, sizeof(current_process->comm));
        strlcpy(current_process->exec_path, path ? path : "",
                sizeof(current_process->exec_path));
    }

    if (current_process->vm_map) {
        vm_map_destroy(current_process->vm_map);
    }
    current_process->vm_map = map;
    arch_set_kernel_stack((uintptr_t)current_thread->kstack_top);

    current_process->arg_start = 0;
    current_process->arg_end = 0;
    proc_capture_cmdline(current_process, kargv);

    rc = aout_build_stack(pmap, kargv, argc, kenvp, envc, 1, &sp);
    if (rc != 0) {
        goto fail;
    }
    aout_free_vector(kargv, argc);
    aout_free_vector(kenvp, envc);
    kfree(libs, COFF_MAX_LIBS * COFF_LIB_PATH_MAX);
    kfree(img, sizeof(*img));

    proc_close_cloexec(current_process);
    kern_close(fd);

    if (coff_debug_enabled()) {
        char b[64];

        snprintf(b, sizeof(b), "COFF: enter eip=0x%x esp=0x%x\n", entry, sp);
        kprint(b);
    }

    pmap_activate((pmap_t)(uintptr_t)current_process->pmap);
    jump_to_userspace(entry, sp, 0);
    return 0;   /* not reached */

fail:
    aout_free_vector(kargv, argc);
    aout_free_vector(kenvp, envc);
    if (libs) {
        kfree(libs, COFF_MAX_LIBS * COFF_LIB_PATH_MAX);
    }
    if (img) {
        kfree(img, sizeof(*img));
    }
    return coff_fail(fd, rc, coff_debug_enabled() ? "COFF: load failed" : NULL);
}

static struct exec_binary_handler coff_handler = {
    .name = "COFF",
    .check = coff_check_file,
    .load = coff_load,
    .next = NULL,
};

void coff_init_handler(void) {
    exec_register_handler(&coff_handler);
}
#endif /* !HOST_TEST */

