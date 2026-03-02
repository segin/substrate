#include "elf_private.h"

typedef struct {
    elf_section_t *section;
    size_t old_index;
} debug_sort_entry_t;

static int str_endswith(const char *s, const char *suffix) {
    size_t slen;
    size_t tlen;
    if (s == NULL || suffix == NULL) {
        return 0;
    }
    slen = strlen(s);
    tlen = strlen(suffix);
    if (slen < tlen) {
        return 0;
    }
    return strcmp(s + (slen - tlen), suffix) == 0;
}

static int is_debug_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return strncmp(name, ".debug_", 7) == 0 || strncmp(name, ".zdebug_", 8) == 0;
}

int elf_section_is_debug(const elf_section_t *section) {
    const char *name;
    if (section == NULL) {
        return 0;
    }
    name = elf_section_name(section);
    if (is_debug_name(name)) {
        return 1;
    }
    return strcmp(name ? name : "", ".gdb_index") == 0;
}

int elf_section_is_cfi(const elf_section_t *section) {
    const char *name;
    if (section == NULL) {
        return 0;
    }
    name = elf_section_name(section);
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, ".eh_frame") == 0 || strcmp(name, ".eh_frame_hdr") == 0 ||
           strcmp(name, ".debug_frame") == 0;
}

int elf_section_is_split_dwarf(const elf_section_t *section) {
    const char *name;
    if (section == NULL) {
        return 0;
    }
    name = elf_section_name(section);
    if (name == NULL) {
        return 0;
    }
    if (str_endswith(name, ".dwo") || str_endswith(name, ".dwp")) {
        return 1;
    }
    return strstr(name, "_dwo") != NULL;
}

int elf_section_is_compressed_debug(const elf_section_t *section) {
    const char *name;
    if (section == NULL || !elf_section_is_debug(section)) {
        return 0;
    }
    name = elf_section_name(section);
    if (section->flags & SHF_COMPRESSED) {
        return 1;
    }
    return name != NULL && strncmp(name, ".zdebug_", 8) == 0;
}

static int parse_chdr(const elf_section_t *section, uint32_t *ch_type_out, uint64_t *ch_size_out,
                      uint64_t *ch_align_out) {
    const uint8_t *p;
    if (section == NULL || section->obj == NULL || section->data == NULL) {
        return 0;
    }
    if ((section->flags & SHF_COMPRESSED) == 0) {
        return 0;
    }
    p = section->data;
    if (section->obj->cls == ELFOBJ_CLASS_64) {
        if (section->data_size < 24) {
            return 0;
        }
        if (ch_type_out != NULL) {
            *ch_type_out = elf__rd32(p + 0, section->obj->endian);
        }
        if (ch_size_out != NULL) {
            *ch_size_out = elf__rd64(p + 8, section->obj->endian);
        }
        if (ch_align_out != NULL) {
            *ch_align_out = elf__rd64(p + 16, section->obj->endian);
        }
        return 1;
    }
    if (section->data_size < 12) {
        return 0;
    }
    if (ch_type_out != NULL) {
        *ch_type_out = elf__rd32(p + 0, section->obj->endian);
    }
    if (ch_size_out != NULL) {
        *ch_size_out = elf__rd32(p + 4, section->obj->endian);
    }
    if (ch_align_out != NULL) {
        *ch_align_out = elf__rd32(p + 8, section->obj->endian);
    }
    return 1;
}

elf_err_t elf_debug_set_compression_hint(elf_section_t *section, uint32_t ch_type,
                                         uint64_t uncompressed_size, uint64_t addralign) {
    elfobj_t *obj;
    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!elf_section_is_debug(section)) {
        return ELF_ERR_STATE;
    }
    obj = section->obj;
    if (obj->readonly || obj->finalized) {
        elf__set_err(obj, ELF_ERR_STATE, "cannot mutate finalized/read-only object");
        return ELF_ERR_STATE;
    }
    if (addralign == 0) {
        addralign = 1;
    }
    section->flags |= SHF_COMPRESSED;
    section->has_compression_hint = 1;
    section->compression_type = ch_type;
    section->compression_size = uncompressed_size;
    section->compression_addralign = addralign;
    obj->dirty = 1;
    return ELF_OK;
}

int elf_debug_get_compression_hint(const elf_section_t *section, uint32_t *ch_type_out,
                                   uint64_t *uncompressed_size_out, uint64_t *addralign_out) {
    if (section == NULL) {
        return 0;
    }
    if (section->has_compression_hint) {
        if (ch_type_out != NULL) {
            *ch_type_out = section->compression_type;
        }
        if (uncompressed_size_out != NULL) {
            *uncompressed_size_out = section->compression_size;
        }
        if (addralign_out != NULL) {
            *addralign_out = section->compression_addralign;
        }
        return 1;
    }
    return parse_chdr(section, ch_type_out, uncompressed_size_out, addralign_out);
}

static uint64_t read_uint_width(const uint8_t *p, elfobj_endian_t e, int width) {
    if (width == 8) {
        return elf__rd64(p, e);
    }
    return elf__rd32(p, e);
}

static elf_err_t cfi_stats(const elf_section_t *section, size_t *cie_count_out, size_t *fde_count_out) {
    const uint8_t *data;
    size_t size;
    size_t off = 0;
    size_t cie_count = 0;
    size_t fde_count = 0;
    int is_debug_frame;

    if (section == NULL || section->obj == NULL || cie_count_out == NULL || fde_count_out == NULL) {
        return ELF_ERR_STATE;
    }
    data = section->data;
    size = section->data_size;
    is_debug_frame = strcmp(section->name ? section->name : "", ".debug_frame") == 0;

    while (off + 4 <= size) {
        uint32_t len32 = elf__rd32(data + off, section->obj->endian);
        size_t len_field_size = 4;
        uint64_t payload_size = len32;
        uint64_t total_size;
        uint64_t cie_id;
        int id_width = 4;

        if (len32 == 0) {
            off += 4;
            break;
        }
        if (len32 == 0xffffffffu) {
            if (off + 12 > size) {
                return ELF_ERR_FORMAT;
            }
            len_field_size = 12;
            payload_size = elf__rd64(data + off + 4, section->obj->endian);
            id_width = 8;
        }
        if (payload_size < (uint64_t)id_width) {
            return ELF_ERR_FORMAT;
        }
        if (!elf__u64_add((uint64_t)len_field_size, payload_size, &total_size)) {
            return ELF_ERR_BOUNDS;
        }
        if (total_size > SIZE_MAX || off + (size_t)total_size > size) {
            return ELF_ERR_FORMAT;
        }
        cie_id = read_uint_width(data + off + len_field_size, section->obj->endian, id_width);
        if (is_debug_frame) {
            uint64_t cie_marker = id_width == 8 ? UINT64_MAX : 0xffffffffu;
            if (cie_id == cie_marker) {
                cie_count++;
            } else {
                fde_count++;
            }
        } else {
            if (cie_id == 0) {
                cie_count++;
            } else {
                fde_count++;
            }
        }
        off += (size_t)total_size;
    }

    if (off != size) {
        size_t i;
        for (i = off; i < size; ++i) {
            if (data[i] != 0) {
                return ELF_ERR_FORMAT;
            }
        }
    }

    *cie_count_out = cie_count;
    *fde_count_out = fde_count;
    return ELF_OK;
}

elf_err_t elf_eh_frame_stats(const elf_section_t *section, size_t *cie_count_out, size_t *fde_count_out) {
    if (section == NULL) {
        return ELF_ERR_STATE;
    }
    if (!elf_section_is_cfi(section)) {
        return ELF_ERR_STATE;
    }
    return cfi_stats(section, cie_count_out, fde_count_out);
}

static int dwarf_unit_stream_section(const char *name) {
    return strcmp(name, ".debug_info") == 0 || strcmp(name, ".debug_types") == 0 ||
           strcmp(name, ".debug_line") == 0 || strcmp(name, ".debug_loclists") == 0 ||
           strcmp(name, ".debug_rnglists") == 0 || strcmp(name, ".debug_addr") == 0;
}

static elf_err_t validate_dwarf_unit_stream(const elf_section_t *section) {
    const uint8_t *data;
    size_t size;
    size_t off = 0;

    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (section->data == NULL || section->data_size == 0) {
        return ELF_OK;
    }
    data = section->data;
    size = section->data_size;

    while (off + 4 <= size) {
        uint32_t len32 = elf__rd32(data + off, section->obj->endian);
        size_t len_field_size = 4;
        uint64_t payload_size = len32;
        uint64_t total_size;
        if (len32 == 0) {
            break;
        }
        if (len32 == 0xffffffffu) {
            if (off + 12 > size) {
                return ELF_ERR_FORMAT;
            }
            len_field_size = 12;
            payload_size = elf__rd64(data + off + 4, section->obj->endian);
        }
        if (!elf__u64_add((uint64_t)len_field_size, payload_size, &total_size)) {
            return ELF_ERR_BOUNDS;
        }
        if (total_size > SIZE_MAX || off + (size_t)total_size > size) {
            return ELF_ERR_FORMAT;
        }
        off += (size_t)total_size;
    }
    if (off != size) {
        return ELF_ERR_FORMAT;
    }
    return ELF_OK;
}

static int dwarf_frame_pointer_reg(uint16_t machine) {
    if (machine == EM_ARM) {
        return 11;
    }
    if (machine == EM_AARCH64) {
        return 29;
    }
    if (machine == EM_MIPS) {
        return 30;
    }
    if (machine == EM_RISCV) {
        return 8;
    }
    if (machine == EM_LOONGARCH) {
        return 22;
    }
    if (machine == EM_68K) {
        return 14;
    }
    if (machine == EM_VAX) {
        return 13;
    }
    if (machine == EM_PPC || machine == EM_PPC64) {
        return 1;
    }
    if (machine == EM_ALPHA) {
        return 15;
    }
    if (machine == EM_386) {
        return 5;
    }
    if (machine == EM_X86_64) {
        return 6;
    }
    return -1;
}

static int dwarf_link_register_reg(uint16_t machine) {
    if (machine == EM_AARCH64) {
        return 30;
    }
    if (machine == EM_ARM) {
        return 14;
    }
    if (machine == EM_MIPS) {
        return 31;
    }
    if (machine == EM_RISCV) {
        return 1;
    }
    if (machine == EM_LOONGARCH) {
        return 1;
    }
    if (machine == EM_68K) {
        return -1;
    }
    if (machine == EM_VAX) {
        return 15;
    }
    if (machine == EM_PPC || machine == EM_PPC64) {
        return 108;
    }
    if (machine == EM_ALPHA) {
        return 26;
    }
    return -1;
}

static int validate_arch_cfi_presence(elfobj_t *obj, int *found_cfi) {
    size_t i;
    *found_cfi = 0;
    for (i = 0; i < obj->section_count; ++i) {
        if (elf_section_is_cfi(obj->sections[i])) {
            *found_cfi = 1;
            return 0;
        }
    }
    return 0;
}

elf_err_t elf_debug_validate(elfobj_t *obj, char **diagnostics) {
    size_t i;
    int has_error = 0;
    elf_err_t base;
    int have_cfi = 0;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }

    base = elf_validate(obj, NULL);
    if (base != ELF_OK) {
        has_error = 1;
    }

    for (i = 0; i < obj->section_count; ++i) {
        const elf_section_t *sec = obj->sections[i];
        if (sec == NULL || sec->name == NULL) {
            continue;
        }
        if (!elf_section_is_debug(sec) && !elf_section_is_cfi(sec) && !elf_section_is_split_dwarf(sec)) {
            continue;
        }

        if (elf_section_is_compressed_debug(sec)) {
            uint32_t ch_type;
            uint64_t uncompressed;
            uint64_t align;
            if (!elf_debug_get_compression_hint(sec, &ch_type, &uncompressed, &align)) {
                has_error = 1;
                (void)elf__append_diag_fmt(obj, "compressed debug section malformed index=", i);
            }
        }

        if (elf_section_is_cfi(sec)) {
            size_t cie_count;
            size_t fde_count;
            if (elf_eh_frame_stats(sec, &cie_count, &fde_count) != ELF_OK) {
                has_error = 1;
                (void)elf__append_diag_fmt(obj, "CFI section malformed index=", i);
            }
        }

        if (dwarf_unit_stream_section(sec->name) && !elf_section_is_compressed_debug(sec)) {
            if (validate_dwarf_unit_stream(sec) != ELF_OK) {
                has_error = 1;
                (void)elf__append_diag_fmt(obj, "DWARF unit stream malformed index=", i);
            }
        }
    }

    for (i = 0; i < obj->reloc_count; ++i) {
        const elf_reloc_t *r = obj->relocs[i];
        if (r == NULL || r->section == NULL) {
            continue;
        }
        if (!elf_section_is_debug(r->section) && !elf_section_is_cfi(r->section) &&
            !elf_section_is_split_dwarf(r->section)) {
            continue;
        }
        if (r->symbol == NULL || r->symbol->name == NULL) {
            has_error = 1;
            (void)elf__append_diag_fmt(obj, "debug relocation missing symbol index=", i);
        }
    }

    (void)validate_arch_cfi_presence(obj, &have_cfi);
    if ((obj->machine == EM_ARM || obj->machine == EM_AARCH64) && !have_cfi) {
        (void)elf__diag_append(obj, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                               "no CFI section found for target architecture");
    }
    if (obj->machine == EM_ARM || obj->machine == EM_AARCH64 || obj->machine == EM_MIPS ||
        obj->machine == EM_LOONGARCH || obj->machine == EM_68K ||
        obj->machine == EM_VAX || obj->machine == EM_PPC || obj->machine == EM_PPC64 ||
        obj->machine == EM_ALPHA ||
        obj->machine == EM_RISCV) {
        int fp = dwarf_frame_pointer_reg(obj->machine);
        int lr = dwarf_link_register_reg(obj->machine);
        char msg[160];
        if (obj->machine == EM_MIPS) {
            (void)snprintf(msg, sizeof(msg),
                           "DWARF frame model fp=%d lr=%d regs gpr=0-31 fpr=32-63 hi=64 lo=65",
                           fp, lr);
        } else if (obj->machine == EM_LOONGARCH) {
            (void)snprintf(msg, sizeof(msg),
                           "DWARF frame model fp=%d lr=%d regs r0-31=0-31 f0-31=32-63", fp,
                           lr);
        } else if (obj->machine == EM_RISCV) {
            (void)snprintf(msg, sizeof(msg),
                           "DWARF frame model fp=%d lr=%d regs x0-31=0-31 f0-31=32-63", fp,
                           lr);
        } else if (obj->machine == EM_68K) {
            (void)snprintf(msg, sizeof(msg),
                           "DWARF frame model fp=%d lr=stack regs d0-7=0-7 a0-7=8-15 fp0-7=16-23",
                           fp);
        } else if (obj->machine == EM_VAX) {
            (void)snprintf(msg, sizeof(msg),
                           "DWARF frame model fp=%d lr=%d regs r0-15=0-15 ap=12 fp=13 sp=14 pc=15",
                           fp, lr);
        } else if (obj->machine == EM_PPC) {
            (void)snprintf(msg, sizeof(msg),
                           "DWARF frame model fp=%d lr=%d regs r0-31=0-31 f0-31=32-63 cr=68-75",
                           fp, lr);
        } else if (obj->machine == EM_PPC64) {
            (void)snprintf(msg, sizeof(msg),
                           "DWARF frame model fp=%d lr=%d regs r0-31=0-31 f0-31=32-63 v0-31=77-108",
                           fp, lr);
        } else if (obj->machine == EM_ALPHA) {
            (void)snprintf(msg, sizeof(msg),
                           "DWARF frame model fp=%d lr=%d regs r0-30=0-30 f0-30=32-62 sp=30 ra=26",
                           fp, lr);
        } else {
            (void)snprintf(msg, sizeof(msg), "DWARF frame model fp=%d lr=%d", fp, lr);
        }
        (void)elf__diag_append(obj, ELF_DIAG_INFO, ELF_OK, UINT64_MAX, msg);
    }

    if (diagnostics != NULL) {
        if (obj->diag.buf != NULL) {
            *diagnostics = elf__strdup(obj->diag.buf);
        } else {
            *diagnostics = elf__strdup("");
        }
        if (*diagnostics == NULL) {
            return ELF_ERR_OOM;
        }
    }
    return has_error ? ELF_ERR_FORMAT : ELF_OK;
}

static int debug_sort_cmp(const void *a, const void *b) {
    const debug_sort_entry_t *aa = (const debug_sort_entry_t *)a;
    const debug_sort_entry_t *bb = (const debug_sort_entry_t *)b;
    const char *an = aa->section->name ? aa->section->name : "";
    const char *bn = bb->section->name ? bb->section->name : "";
    int c = strcmp(an, bn);
    if (c != 0) {
        return c;
    }
    if (aa->section->type < bb->section->type) {
        return -1;
    }
    if (aa->section->type > bb->section->type) {
        return 1;
    }
    if (aa->old_index < bb->old_index) {
        return -1;
    }
    if (aa->old_index > bb->old_index) {
        return 1;
    }
    return 0;
}

elf_err_t elf_debug_sort_sections(elfobj_t *obj) {
    size_t i;
    size_t n = 0;
    size_t *positions;
    debug_sort_entry_t *entries;
    elf_err_t err = ELF_OK;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }

    positions = (size_t *)elf__calloc(obj->section_count ? obj->section_count : 1, sizeof(positions[0]));
    entries = (debug_sort_entry_t *)elf__calloc(obj->section_count ? obj->section_count : 1, sizeof(entries[0]));
    if (positions == NULL || entries == NULL) {
        free(positions);
        free(entries);
        return ELF_ERR_OOM;
    }

    for (i = 0; i < obj->section_count; ++i) {
        elf_section_t *sec = obj->sections[i];
        if (sec == NULL) {
            continue;
        }
        if (!elf_section_is_debug(sec) && !elf_section_is_cfi(sec) && !elf_section_is_split_dwarf(sec)) {
            continue;
        }
        positions[n] = i;
        entries[n].section = sec;
        entries[n].old_index = i;
        n++;
    }

    if (n <= 1) {
        free(positions);
        free(entries);
        return ELF_OK;
    }

    qsort(entries, n, sizeof(entries[0]), debug_sort_cmp);
    for (i = 0; i < n; ++i) {
        err = elf_reorder_section(obj, entries[i].section, positions[i]);
        if (err != ELF_OK) {
            break;
        }
    }

    free(positions);
    free(entries);
    return err;
}
