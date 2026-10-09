/*
 * ld_ehframe.c -- .eh_frame_hdr.
 */

#include "ld.h"

/* How many FDEs `data` holds; -1 if it is not a run of records. */
static long eh_frame_count_fdes(const uint8_t *data, size_t size, elfobj_endian_t e) {
    size_t off = 0;
    long n = 0;

    while (off + 4 <= size) {
        uint32_t len = read_u32_endian(data + off, e);

        if (len == 0) {
            off += 4;           /* a terminator: one input's, or the last */
            continue;
        }
        if (len == 0xffffffffu || len < 4 || off + 4 + (size_t)len > size) {
            return -1;
        }
        if (read_u32_endian(data + off + 4, e) != 0) {
            n++;
        }
        off += 4 + (size_t)len;
    }
    return n;
}

int plan_eh_frame_hdr(const ld_ctx_t *ctx, elfobj_t *out) {
    elf_section_t *frame = elf_find_section(out, ".eh_frame");
    elf_section_t *hdr;
    const uint8_t *data;
    size_t size = 0;
    long n;

    if (!ctx->opt.eh_frame_hdr || frame == NULL || elf_type(out) == ET_REL ||
        elf_find_section(out, ".eh_frame_hdr") != NULL) {
        return 0;
    }
    data = (const uint8_t *)elf_section_data(frame, &size);
    n = data != NULL ? eh_frame_count_fdes(data, size, elf_endian(out)) : -1;
    if (n <= 0) {
        return 0;               /* nothing to look up, or not to be read */
    }
    hdr = elf_add_section(out, ".eh_frame_hdr", SHT_PROGBITS, SHF_ALLOC);
    if (hdr == NULL || elf_section_set_align(hdr, 4) != ELF_OK ||
        set_section_zero_data(hdr, 12 + 8 * (size_t)n) != 0) {
        return -1;
    }
    return 0;
}

static int eh_entry_cmp(const void *a, const void *b) {
    const eh_entry_t *x = (const eh_entry_t *)a;
    const eh_entry_t *y = (const eh_entry_t *)b;

    return x->start < y->start ? -1 : x->start > y->start;
}

/* The encoding a CIE gives the addresses in its FDEs (its 'R'
 * augmentation); absolute pointers if it says nothing; -1 if the CIE is
 * not one this can read. */
static int eh_cie_fde_encoding(const uint8_t *cie, size_t len, int ptr_size) {
    const uint8_t *p = cie + 8;         /* past the length and the zero id */
    const uint8_t *end = cie + 4 + len;
    const char *aug;
    uint8_t version;

    if (p >= end) {
        return -1;
    }
    version = *p++;
    aug = (const char *)p;
    while (p < end && *p != 0) {
        p++;
    }
    if (p >= end || (version != 1 && version != 3)) {
        return -1;
    }
    p++;
    if (aug[0] == 'e' && aug[1] == 'h') {
        p += ptr_size;
        aug += 2;
    }
    while (p < end && (*p++ & 0x80) != 0) {     /* code alignment */
    }
    while (p < end && (*p++ & 0x80) != 0) {     /* data alignment */
    }
    if (version == 1) {
        p++;                                    /* return address register */
    } else {
        while (p < end && (*p++ & 0x80) != 0) {
        }
    }
    if (aug[0] != 'z') {
        return DW_EH_PE_absptr;
    }
    while (p < end && (*p++ & 0x80) != 0) {     /* augmentation length */
    }
    for (aug++; *aug != '\0' && p < end; ++aug) {
        if (*aug == 'R') {
            return *p;
        }
        if (*aug == 'L') {
            p++;
        } else if (*aug == 'P') {
            int enc = *p++ & 0x0f;

            p += enc == DW_EH_PE_absptr ? ptr_size
               : enc == DW_EH_PE_udata4 || enc == DW_EH_PE_sdata4 ? 4
               : enc == DW_EH_PE_udata8 || enc == DW_EH_PE_sdata8 ? 8 : 0;
            if (enc != DW_EH_PE_absptr && enc != DW_EH_PE_udata4 && enc != DW_EH_PE_sdata4 &&
                enc != DW_EH_PE_udata8 && enc != DW_EH_PE_sdata8) {
                return -1;
            }
        } else if (*aug != 'S' && *aug != 'B') {
            return -1;
        }
    }
    return DW_EH_PE_absptr;
}

int fill_eh_frame_hdr(elfobj_t *out) {
    elf_section_t *frame = elf_find_section(out, ".eh_frame");
    elf_section_t *hdr = elf_find_section(out, ".eh_frame_hdr");
    elfobj_endian_t e = elf_endian(out);
    int ptr_size = elf_class(out) == ELFOBJ_CLASS_64 ? 8 : 4;
    const uint8_t *data;
    uint8_t *buf;
    eh_entry_t *ents;
    size_t size = 0, hsize = 0, off = 0, cap, n = 0, i;
    uint64_t faddr, haddr;
    int usable = 1;
    int rc;

    if (frame == NULL || hdr == NULL || (hsize = (size_t)elf_section_size(hdr)) < 12) {
        return 0;
    }
    data = (const uint8_t *)elf_section_data(frame, &size);
    cap = (hsize - 12) / 8;
    buf = (uint8_t *)calloc(1, hsize);
    ents = (eh_entry_t *)calloc(cap != 0 ? cap : 1, sizeof(*ents));
    if (data == NULL || buf == NULL || ents == NULL) {
        free(buf);
        free(ents);
        return -1;
    }
    faddr = elf_section_addr(frame);
    haddr = elf_section_addr(hdr);
    while (usable && off + 4 <= size) {
        uint32_t len = read_u32_endian(data + off, e);
        uint32_t id;
        size_t cie_off;
        uint64_t field, start;
        int enc;

        if (len == 0) {
            off += 4;
            continue;
        }
        if (len == 0xffffffffu || len < 4 || off + 4 + (size_t)len > size) {
            usable = 0;
            break;
        }
        id = read_u32_endian(data + off + 4, e);
        if (id == 0) {
            off += 4 + (size_t)len;
            continue;
        }
        /* An FDE: its CIE is `id` bytes back from the id. */
        cie_off = off + 4 - (size_t)id;
        if ((size_t)id > off + 4 || cie_off + 8 > size || read_u32_endian(data + cie_off + 4, e) != 0 ||
            (enc = eh_cie_fde_encoding(data + cie_off, read_u32_endian(data + cie_off, e), ptr_size)) < 0) {
            usable = 0;
            break;
        }
        field = faddr + off + 8;
        switch (enc & 0x0f) {
        case DW_EH_PE_absptr:
            start = ptr_size == 8 ? read_u64_endian(data + off + 8, e) : read_u32_endian(data + off + 8, e);
            break;
        case DW_EH_PE_udata4:
            start = read_u32_endian(data + off + 8, e);
            break;
        case DW_EH_PE_sdata4:
            start = (uint64_t)(int64_t)(int32_t)read_u32_endian(data + off + 8, e);
            break;
        case DW_EH_PE_udata8:
        case DW_EH_PE_sdata8:
            start = read_u64_endian(data + off + 8, e);
            break;
        default:
            usable = 0;
            continue;
        }
        if ((enc & 0x70) == DW_EH_PE_pcrel) {
            start += field;
        } else if ((enc & 0x70) != 0) {
            usable = 0;
            continue;
        }
        if (ptr_size == 4) {
            start &= 0xffffffffu;
        }
        /* The FDE of a function that was left out (a second copy of an
         * inline one) describes nothing; one for an address in no code of
         * this output is such a one. */
        for (i = 0; i < elf_section_count(out); ++i) {
            const elf_section_t *sec = elf_section_get(out, i);

            if (sec != NULL && (elf_section_flags(sec) & (SHF_ALLOC | SHF_EXECINSTR)) == (SHF_ALLOC | SHF_EXECINSTR) &&
                start >= elf_section_addr(sec) && start < elf_section_addr(sec) + elf_section_size(sec)) {
                break;
            }
        }
        if (i < elf_section_count(out) && n < cap) {
            ents[n].start = (int64_t)(start - haddr);
            ents[n].fde = (int64_t)(faddr + off - haddr);
            n++;
        }
        off += 4 + (size_t)len;
    }
    buf[0] = 1;                                         /* version */
    buf[1] = DW_EH_PE_pcrel | DW_EH_PE_sdata4;          /* .eh_frame's address */
    write_u32_endian(buf + 4, e, (uint32_t)(faddr - (haddr + 4)));
    if (usable) {
        qsort(ents, n, sizeof(*ents), eh_entry_cmp);
        buf[2] = DW_EH_PE_udata4;                       /* the count */
        buf[3] = DW_EH_PE_datarel | DW_EH_PE_sdata4;    /* the table: from this section */
        write_u32_endian(buf + 8, e, (uint32_t)n);
        for (i = 0; i < n; ++i) {
            write_u32_endian(buf + 12 + 8 * i, e, (uint32_t)ents[i].start);
            write_u32_endian(buf + 16 + 8 * i, e, (uint32_t)ents[i].fde);
        }
    } else {
        /* Frames in a form this does not read: no table, and the
         * unwinder reads .eh_frame through, as it would with no header. */
        buf[2] = DW_EH_PE_omit;
        buf[3] = DW_EH_PE_omit;
    }
    rc = elf_section_set_data(hdr, buf, hsize) == ELF_OK ? 0 : -1;
    free(buf);
    free(ents);
    return rc;
}
