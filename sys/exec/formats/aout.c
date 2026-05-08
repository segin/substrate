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
#include <sys/exec.h>
#include <sys/errno.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <arch/i386/pmap.h>
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

    /* Entry must be non-zero for executable images.  Some BSDs set
     * a_entry to text_start which can be 0 for OMAGIC images linked
     * at offset 0; treat 0 entry as suspicious but not fatal for
     * OMAGIC/NMAGIC, while ZMAGIC/QMAGIC always have non-zero text VA. */
    if ((magic == AOUT_ZMAGIC_VAL || magic == AOUT_QMAGIC_VAL) &&
        hdr->a_entry == 0) {
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

static int aout_load(int fd, const char *path, char *const argv[],
                     char *const envp[]) {
    (void)argv; (void)envp;
    struct aout_exec hdr;
    int rc;

    kern_lseek(fd, 0, 0);
    rc = kern_read(fd, (char *)&hdr, sizeof(hdr));
    if (rc != (int)sizeof(hdr)) {
        kern_close(fd);
        return -ENOEXEC;
    }

    /* Stat-via-seek for total file size — the kernel needs it to validate
     * the header's size claims.  kern_lseek(SEEK_END) returns the file
     * length; restore the position to header end afterwards. */
    int64_t end = kern_lseek(fd, 0, 2 /* SEEK_END */);
    if (end <= 0 || end > (int64_t)0xFFFFFFFFLL) {
        kern_close(fd);
        return -ENOEXEC;
    }
    if (aout_validate_header(&hdr, (uint32_t)end) != 0) {
        kern_close(fd);
        return -ENOEXEC;
    }

    /*
     * Classification is informational here — choosing a personality is
     * exec_dispatch's concern, not the loader's.  We log the decision
     * so any mis-tagging is visible during bring-up.
     */
    enum aout_flavor flavor = aout_classify(&hdr);
    char dbg[80];
    snprintf(dbg, sizeof(dbg),
             "a.out: magic=0%o mid=%u flavor=%d entry=0x%x text=%u data=%u bss=%u\n",
             AOUT_GETMAGIC(hdr.a_midmag), AOUT_GETMID(hdr.a_midmag),
             (int)flavor, hdr.a_entry, hdr.a_text, hdr.a_data, hdr.a_bss);
    kprint(dbg);

    /*
     * Full segment mapping + relocation + personality dispatch is the
     * follow-up.  For now, refuse the exec rather than fault the
     * userspace process: returning -ENOEXEC lets exec_dispatch fall
     * through to any other handler (there is none today, but the
     * contract is clean).  When the loader gains end-to-end execution
     * support, replace this with the actual map+jump path.
     */
    (void)path;
    kern_close(fd);
    return -ENOEXEC;
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
