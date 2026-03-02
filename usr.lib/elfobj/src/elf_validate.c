#include "elf_private.h"

typedef struct {
    elfobj_t *obj;
    elf_validate_mode_t mode;
    size_t max_errors;
    size_t error_count;
    int has_error;
} validate_ctx_t;

static int ranges_overlap(uint64_t a_off, uint64_t a_sz, uint64_t b_off, uint64_t b_sz) {
    uint64_t a_end;
    uint64_t b_end;
    if (a_sz == 0 || b_sz == 0) {
        return 0;
    }
    if (!elf__u64_add(a_off, a_sz, &a_end) || !elf__u64_add(b_off, b_sz, &b_end)) {
        return 1;
    }
    return a_off < b_end && b_off < a_end;
}

static int segment_range(const struct elf_segment *seg, const elfobj_t *obj, uint64_t *out_lo, uint64_t *out_hi) {
    size_t i;
    int seen = 0;
    uint64_t lo = UINT64_MAX;
    uint64_t hi = 0;

    for (i = 0; i < seg->section_count; ++i) {
        size_t sidx = seg->section_indices[i];
        uint64_t end;
        const struct elf_section *s;
        if (sidx >= obj->section_count) {
            continue;
        }
        s = obj->sections[sidx];
        if (s == NULL || s->type == SHT_NOBITS || s->size == 0) {
            continue;
        }
        if (!elf__u64_add(s->offset, s->size, &end)) {
            return 0;
        }
        if (s->offset < lo) {
            lo = s->offset;
        }
        if (end > hi) {
            hi = end;
        }
        seen = 1;
    }
    if (!seen) {
        return 0;
    }
    *out_lo = lo;
    *out_hi = hi;
    return 1;
}

static int report_diag(validate_ctx_t *ctx, elf_diag_level_t level, elf_err_t code,
                       uint64_t index, const char *msg) {
    if (ctx == NULL || ctx->obj == NULL || msg == NULL) {
        return 1;
    }
    (void)elf__diag_append(ctx->obj, level, code, index, msg);
    if (level == ELF_DIAG_ERROR) {
        ctx->has_error = 1;
        ctx->error_count++;
        if (ctx->max_errors != 0 && ctx->error_count >= ctx->max_errors) {
            (void)elf__diag_append(ctx->obj, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                   "validation truncated at error limit");
            return 1;
        }
    }
    return 0;
}

static int report_diag_fmt(validate_ctx_t *ctx, elf_diag_level_t level, elf_err_t code,
                           uint64_t index, const char *prefix, uint64_t value) {
    char buf[160];
    if (prefix == NULL) {
        return report_diag(ctx, level, code, index, "validation issue");
    }
    (void)snprintf(buf, sizeof(buf), "%s%llu",
                   prefix, (unsigned long long)value);
    return report_diag(ctx, level, code, index, buf);
}

static elf_diag_level_t strictness_level(const validate_ctx_t *ctx) {
    if (ctx->mode == ELF_VALIDATE_STRICT) {
        return ELF_DIAG_ERROR;
    }
    return ELF_DIAG_WARNING;
}

static int section_is_allocated(const struct elf_section *s) {
    if (s == NULL) {
        return 0;
    }
    return (s->flags & SHF_ALLOC) != 0 && s->type != SHT_NOBITS && s->size != 0;
}

static int is_exec_section(const struct elf_section *s) {
    return s != NULL && (s->flags & SHF_EXECINSTR) != 0;
}

static int has_mapping_symbol(const elfobj_t *obj, char tag0, char tag1) {
    size_t i;
    for (i = 0; i < obj->symbol_count; ++i) {
        const struct elf_symbol *sym = obj->symbols[i];
        if (sym == NULL || sym->name == NULL) {
            continue;
        }
        if (sym->name[0] == '$' && (sym->name[1] == tag0 || sym->name[1] == tag1)) {
            return 1;
        }
    }
    return 0;
}

static int validate_arm_specific(validate_ctx_t *ctx, const elfobj_t *obj) {
    size_t i;
    int have_exidx = 0;
    int have_attrs = 0;
    int have_exec = 0;

    for (i = 0; i < obj->section_count; ++i) {
        const struct elf_section *s = obj->sections[i];
        if (s == NULL) {
            continue;
        }
        if (s->type == SHT_ARM_EXIDX) {
            have_exidx = 1;
            if ((s->flags & SHF_LINK_ORDER) == 0) {
                if (report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                "SHT_ARM_EXIDX missing SHF_LINK_ORDER")) {
                    return 1;
                }
            }
        }
        if (s->type == SHT_ARM_ATTRIBUTES || (s->name != NULL &&
                                              strcmp(s->name, ".ARM.attributes") == 0)) {
            have_attrs = 1;
            if (s->data_size == 0 || s->data == NULL || s->data[0] != 'A') {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, i,
                                ".ARM.attributes has invalid payload")) {
                    return 1;
                }
            }
        }
        if (is_exec_section(s)) {
            have_exec = 1;
            if (s->addralign < 2) {
                if (report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                "ARM code section alignment must be >= 2")) {
                    return 1;
                }
            }
            if (s->name != NULL && strstr(s->name, ".text") != NULL && s->addralign < 4) {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, i,
                                "ARM text section alignment should be >= 4")) {
                    return 1;
                }
            }
        }
    }

    if (have_exec && !has_mapping_symbol(obj, 'a', 't')) {
        if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                        "ARM executable sections missing $a/$t mapping symbols")) {
            return 1;
        }
    }
    if (have_exec && !has_mapping_symbol(obj, 'd', 'd')) {
        if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                        "ARM executable sections missing $d mapping symbols")) {
            return 1;
        }
    }
    if (!have_attrs) {
        if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                        "ARM object missing .ARM.attributes section")) {
            return 1;
        }
    }

    for (i = 0; i < obj->phdr_count; ++i) {
        const struct elf_phdr *ph = &obj->phdrs[i];
        if (ph->type == PT_ARM_EXIDX && !have_exidx) {
            if (report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                            "PT_ARM_EXIDX present but no SHT_ARM_EXIDX section")) {
                return 1;
            }
        }
    }
    return 0;
}

static int validate_note_gnu_property_aarch64(validate_ctx_t *ctx, const struct elf_section *s,
                                               size_t index) {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    size_t off;
    if (s->data == NULL || s->data_size < 16) {
        return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                           ".note.gnu.property payload too small");
    }
    namesz = elf__rd32(s->data + 0, s->obj->endian);
    descsz = elf__rd32(s->data + 4, s->obj->endian);
    type = elf__rd32(s->data + 8, s->obj->endian);
    if (type != 5u) {
        return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                           ".note.gnu.property unexpected note type");
    }
    off = 12;
    off = (off + namesz + 3u) & ~3u;
    if (off + descsz > s->data_size) {
        return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_BOUNDS, index,
                           ".note.gnu.property desc out of bounds");
    }
    while (descsz >= 8 && off + 8 <= s->data_size) {
        uint32_t pr_type = elf__rd32(s->data + off, s->obj->endian);
        uint32_t pr_datasz = elf__rd32(s->data + off + 4, s->obj->endian);
        uint32_t bits = 0;
        off += 8;
        if (off + pr_datasz > s->data_size) {
            return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_BOUNDS, index,
                               ".note.gnu.property item out of bounds");
        }
        if (pr_type == GNU_PROPERTY_AARCH64_FEATURE_1_AND && pr_datasz >= 4) {
            bits = elf__rd32(s->data + off, s->obj->endian);
            if ((bits & ~(GNU_PROPERTY_AARCH64_FEATURE_1_BTI |
                          GNU_PROPERTY_AARCH64_FEATURE_1_PAC)) != 0) {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                                "AArch64 GNU property has unknown feature bits")) {
                    return 1;
                }
            }
        }
        off += pr_datasz;
        off = (off + 7u) & ~7u;
        if (descsz < 8 + pr_datasz) {
            break;
        }
        descsz -= 8 + pr_datasz;
    }
    return 0;
}

static uint64_t read_uleb_local(const uint8_t *p, size_t sz, size_t *off, int *ok) {
    uint64_t v = 0;
    unsigned shift = 0;
    *ok = 0;
    while (*off < sz && shift < 64) {
        uint8_t b = p[*off];
        (*off)++;
        v |= (uint64_t)(b & 0x7f) << shift;
        if ((b & 0x80u) == 0) {
            *ok = 1;
            return v;
        }
        shift += 7;
    }
    return 0;
}

static int validate_x86_gnu_property(validate_ctx_t *ctx, const struct elf_section *s, size_t index) {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t ntype;
    size_t align = s->obj->cls == ELFOBJ_CLASS_64 ? 8 : 4;
    size_t off;

    if (s->data == NULL || s->data_size < 16) {
        return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                           ".note.gnu.property payload too small");
    }
    if ((s->addralign % align) != 0) {
        if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                        ".note.gnu.property alignment mismatch")) {
            return 1;
        }
    }
    namesz = elf__rd32(s->data + 0, s->obj->endian);
    descsz = elf__rd32(s->data + 4, s->obj->endian);
    ntype = elf__rd32(s->data + 8, s->obj->endian);
    if (ntype != 5u) {
        if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                        ".note.gnu.property unexpected note type")) {
            return 1;
        }
    }
    off = 12;
    off = (off + namesz + 3u) & ~3u;
    if (off + descsz > s->data_size) {
        return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_BOUNDS, index,
                           ".note.gnu.property desc out of bounds");
    }
    while (descsz >= 8 && off + 8 <= s->data_size) {
        uint32_t t = elf__rd32(s->data + off, s->obj->endian);
        uint32_t sz = elf__rd32(s->data + off + 4, s->obj->endian);
        uint32_t bits = 0;
        off += 8;
        if (off + sz > s->data_size) {
            return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_BOUNDS, index,
                               ".note.gnu.property item out of bounds");
        }
        if (sz >= 4) {
            bits = elf__rd32(s->data + off, s->obj->endian);
        }
        if (t == GNU_PROPERTY_X86_ISA_1_NEEDED || t == GNU_PROPERTY_X86_ISA_1_USED) {
            if ((bits & ~(GNU_PROPERTY_X86_ISA_1_BASELINE | GNU_PROPERTY_X86_ISA_1_V2 |
                          GNU_PROPERTY_X86_ISA_1_V3 | GNU_PROPERTY_X86_ISA_1_V4)) != 0) {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                                "x86 GNU ISA property has unknown bits")) {
                    return 1;
                }
            }
        } else if (t == GNU_PROPERTY_X86_FEATURE_1_AND) {
            if ((bits & ~(GNU_PROPERTY_X86_FEATURE_1_IBT | GNU_PROPERTY_X86_FEATURE_1_SHSTK)) !=
                0) {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                                "x86 GNU FEATURE_1 property has unknown bits")) {
                    return 1;
                }
            }
        } else {
            if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                            "unknown GNU property type")) {
                return 1;
            }
        }
        off += sz;
        off = (off + align - 1) & ~(align - 1);
        if (descsz < 8 + sz) {
            break;
        }
        descsz -= 8 + sz;
    }
    return 0;
}

static int validate_eh_frame_ra(validate_ctx_t *ctx, const struct elf_section *s, size_t index) {
    size_t off = 0;
    uint32_t len;
    uint32_t cie_id;
    size_t p;
    int ok;
    uint64_t ra_reg;
    uint64_t expected = s->obj->machine == EM_386 ? 8u : 16u;

    if (s->data == NULL || s->data_size < 16) {
        return 0;
    }
    len = elf__rd32(s->data + off, s->obj->endian);
    if (len == 0 || off + 4 + len > s->data_size) {
        return 0;
    }
    cie_id = elf__rd32(s->data + off + 4, s->obj->endian);
    if (cie_id != 0) {
        return 0;
    }
    p = off + 8;
    if (p >= s->data_size) {
        return 0;
    }
    p++; /* version */
    while (p < s->data_size && s->data[p] != '\0') {
        p++;
    }
    if (p >= s->data_size) {
        return 0;
    }
    p++;
    (void)read_uleb_local(s->data, s->data_size, &p, &ok);
    if (!ok) {
        return 0;
    }
    (void)read_uleb_local(s->data, s->data_size, &p, &ok);
    if (!ok) {
        return 0;
    }
    ra_reg = read_uleb_local(s->data, s->data_size, &p, &ok);
    if (!ok) {
        return 0;
    }
    if (ra_reg != expected) {
        return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, index,
                           ".eh_frame CIE return-address register mismatch");
    }
    return 0;
}

static int validate_aarch64_specific(validate_ctx_t *ctx, const elfobj_t *obj) {
    size_t i;
    int have_exec = 0;

    for (i = 0; i < obj->section_count; ++i) {
        const struct elf_section *s = obj->sections[i];
        if (s == NULL) {
            continue;
        }
        if (is_exec_section(s)) {
            have_exec = 1;
            if (s->addralign < 4) {
                if (report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                "AArch64 code section alignment must be >= 4")) {
                    return 1;
                }
            }
        }
        if (s->name != NULL && strcmp(s->name, ".note.gnu.property") == 0) {
            if (validate_note_gnu_property_aarch64(ctx, s, i)) {
                return 1;
            }
        }
    }

    if (have_exec && !has_mapping_symbol(obj, 'x', 'x')) {
        if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                        "AArch64 executable sections missing $x mapping symbols")) {
            return 1;
        }
    }
    if (have_exec && !has_mapping_symbol(obj, 'd', 'd')) {
        if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                        "AArch64 executable sections missing $d mapping symbols")) {
            return 1;
        }
    }
    return 0;
}

static int validate_machine_basics(validate_ctx_t *ctx, const elfobj_t *obj) {
    if (ctx == NULL || obj == NULL) {
        return 1;
    }

    switch (obj->machine) {
        case EM_386:
            if (obj->cls != ELFOBJ_CLASS_32) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_386 requires ELFCLASS32");
            }
            if (obj->endian != ELFOBJ_ENDIAN_LE) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_386 requires little-endian");
            }
            break;
        case EM_X86_64:
            if (obj->cls != ELFOBJ_CLASS_64) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_X86_64 requires ELFCLASS64");
            }
            if (obj->endian != ELFOBJ_ENDIAN_LE) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_X86_64 requires little-endian");
            }
            break;
        case EM_ARM: {
            uint32_t eabi = obj->flags & 0xFF000000u;
            if (obj->cls != ELFOBJ_CLASS_32) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_ARM requires ELFCLASS32");
            }
            if (eabi != 0 && eabi < EF_ARM_ABI_VER5) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_ARM requires EABI version >= 5");
            }
            if ((obj->flags & EF_ARM_ABI_FLOAT_HARD) != 0 &&
                (obj->flags & EF_ARM_ABI_FLOAT_SOFT) != 0) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_ARM has conflicting float ABI flags");
            }
            break;
        }
        case EM_AARCH64:
            if (obj->cls != ELFOBJ_CLASS_64) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_AARCH64 requires ELFCLASS64");
            }
            if (obj->flags != 0 && obj->flags != EF_AARCH64_CHERI_PURECAP) {
                return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_AARCH64 has unknown e_flags bits");
            }
            break;
        case EM_MIPS: {
            elf_mips_abiflags_t af;
            uint32_t arch = obj->flags & 0xf0000000u;
            uint32_t abi = obj->flags & 0x0000f000u;
            if (obj->cls != ELFOBJ_CLASS_32 && obj->cls != ELFOBJ_CLASS_64) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_MIPS requires ELFCLASS32/ELFCLASS64");
            }
            if (obj->endian != ELFOBJ_ENDIAN_LE && obj->endian != ELFOBJ_ENDIAN_BE) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_MIPS requires valid endian encoding");
            }
            if (abi != 0 && abi != EF_MIPS_ABI_O32 && abi != EF_MIPS_ABI_O64 &&
                abi != EF_MIPS_ABI_EABI32 && abi != EF_MIPS_ABI_EABI64) {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                "MIPS e_flags ABI field is not recognized")) {
                    return 1;
                }
            }
            if (elf_mips_abiflags(obj, &af)) {
                if (af.isa_level == 0) {
                    if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                    "MIPS ABIFLAGS missing ISA level")) {
                        return 1;
                    }
                }
                if (arch == EF_MIPS_ARCH_64R6 && af.isa_level < 64) {
                    if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                    "MIPS ABIFLAGS ISA level conflicts with e_flags")) {
                        return 1;
                    }
                }
            }
            break;
        }
        case EM_RISCV: {
            uint32_t known = EF_RISCV_RVC | EF_RISCV_FLOAT_ABI_SINGLE | EF_RISCV_FLOAT_ABI_DOUBLE |
                             EF_RISCV_FLOAT_ABI_QUAD | EF_RISCV_RVE | EF_RISCV_TSO;
            uint32_t f_abi = obj->flags & EF_RISCV_FLOAT_ABI_QUAD;
            if (obj->cls != ELFOBJ_CLASS_32 && obj->cls != ELFOBJ_CLASS_64) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_RISCV requires ELFCLASS32/ELFCLASS64");
            }
            if (obj->endian != ELFOBJ_ENDIAN_LE) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_RISCV requires little-endian");
            }
            if ((obj->flags & ~known) != 0) {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                "RISC-V e_flags contain unknown bits")) {
                    return 1;
                }
            }
            if (f_abi != EF_RISCV_FLOAT_ABI_SOFT && f_abi != EF_RISCV_FLOAT_ABI_SINGLE &&
                f_abi != EF_RISCV_FLOAT_ABI_DOUBLE && f_abi != EF_RISCV_FLOAT_ABI_QUAD) {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                "RISC-V float ABI flags invalid")) {
                    return 1;
                }
            }
            break;
        }
        case EM_LOONGARCH: {
            uint32_t abi = obj->flags & EF_LARCH_ABI_MODIFIER_MASK;
            if (obj->cls != ELFOBJ_CLASS_32 && obj->cls != ELFOBJ_CLASS_64) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_LOONGARCH requires ELFCLASS32/ELFCLASS64");
            }
            if (obj->endian != ELFOBJ_ENDIAN_LE) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_LOONGARCH requires little-endian");
            }
            if (abi != EF_LARCH_ABI_SOFT_FLOAT && abi != EF_LARCH_ABI_SINGLE_FLOAT &&
                abi != EF_LARCH_ABI_DOUBLE_FLOAT) {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                "LoongArch ABI modifier is not recognized")) {
                    return 1;
                }
            }
            if ((obj->flags & ~(EF_LARCH_ABI_MODIFIER_MASK | EF_LARCH_OBJABI_V1)) != 0) {
                if (report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                "LoongArch e_flags contain unknown bits")) {
                    return 1;
                }
            }
            break;
        }
        case EM_68K:
            if (obj->cls != ELFOBJ_CLASS_32) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_68K requires ELFCLASS32");
            }
            if (obj->endian != ELFOBJ_ENDIAN_BE) {
                return report_diag(ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_68K requires big-endian");
            }
            if (obj->flags != 0) {
                return report_diag(ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, UINT64_MAX,
                                   "EM_68K has unknown e_flags bits");
            }
            break;
        default:
            break;
    }
    return 0;
}

elf_err_t elf_validate_ex(elfobj_t *obj, const elf_validate_options_t *options, char **diagnostics) {
    size_t i;
    size_t j;
    int has_layout;
    validate_ctx_t ctx;
    elf_err_t ensure_err;
    elf_validate_mode_t mode = obj != NULL ? obj->validate_mode : ELF_VALIDATE_STRICT;
    size_t max_errors = obj != NULL ? obj->validate_max_errors : 0;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (options != NULL) {
        mode = options->mode;
        max_errors = options->max_errors;
    }
    if (mode != ELF_VALIDATE_PERMISSIVE && mode != ELF_VALIDATE_STRICT) {
        return ELF_ERR_STATE;
    }

    elf__diag_clear(obj);

    memset(&ctx, 0, sizeof(ctx));
    ctx.obj = obj;
    ctx.mode = mode;
    ctx.max_errors = max_errors;
    has_layout = (obj->image != NULL) || obj->finalized;

    ensure_err = elf__ensure_symbols_relocs(obj);
    if (ensure_err != ELF_OK) {
        (void)report_diag(&ctx, ELF_DIAG_ERROR, ensure_err, UINT64_MAX,
                          "failed to materialize symbols/relocations");
        goto done;
    }

    if (validate_machine_basics(&ctx, obj)) {
        goto done;
    }

    if (obj->machine == EM_ARM && validate_arm_specific(&ctx, obj)) {
        goto done;
    }
    if (obj->machine == EM_AARCH64 && validate_aarch64_specific(&ctx, obj)) {
        goto done;
    }

    for (i = 0; i < obj->section_count; ++i) {
        struct elf_section *s = obj->sections[i];
        if (s == NULL) {
            if (report_diag(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i, "NULL section entry")) goto done;
            continue;
        }
        if (s->type != SHT_NOBITS && obj->image != NULL && s->size > 0) {
            if (!elf__bounds_ok((size_t)s->offset, (size_t)s->size, obj->image_size)) {
                if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_BOUNDS, i,
                                    "section out of file bounds index=", i)) goto done;
            }
        }
        if (s->type == SHT_NOBITS && s->data_size != 0) {
            if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                "SHT_NOBITS has payload index=", i)) goto done;
        }
        if ((s->type == SHT_REL || s->type == SHT_RELA) && s->entsize == 0) {
            if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                "relocation section entsize missing index=", i)) goto done;
        }
        if (s->type == SHT_STRTAB && (s->flags & SHF_EXECINSTR) != 0) {
            if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                "string table has executable flag index=", i)) goto done;
        }
        if (obj->machine == EM_RISCV && s->type == SHT_RISCV_ATTRIBUTES) {
            if (s->data == NULL || s->data_size == 0 || s->data[0] != 'A') {
                if (report_diag(&ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, i,
                                "RISC-V attributes section malformed")) {
                    goto done;
                }
            }
        }
        if (obj->machine == EM_386 && s->type == SHT_RELA) {
            if (report_diag(&ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, i,
                            "i386 should use SHT_REL relocations")) {
                goto done;
            }
        }
        if (obj->machine == EM_X86_64 && s->type == SHT_REL) {
            if (report_diag(&ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, i,
                            "x86-64 should use SHT_RELA relocations")) {
                goto done;
            }
        }
        if ((obj->machine == EM_386 || obj->machine == EM_X86_64) &&
            s->name != NULL && strcmp(s->name, ".note.gnu.property") == 0) {
            if (validate_x86_gnu_property(&ctx, s, i)) {
                goto done;
            }
        }
        if ((obj->machine == EM_386 || obj->machine == EM_X86_64) &&
            s->name != NULL && strcmp(s->name, ".eh_frame") == 0) {
            if (validate_eh_frame_ra(&ctx, s, i)) {
                goto done;
            }
        }
    }

    if (has_layout) {
        for (i = 0; i < obj->section_count; ++i) {
            struct elf_section *a = obj->sections[i];
            if (a == NULL || a->type == SHT_NOBITS || a->size == 0) {
                continue;
            }
            for (j = i + 1; j < obj->section_count; ++j) {
                struct elf_section *b = obj->sections[j];
                if (b == NULL || b->type == SHT_NOBITS || b->size == 0) {
                    continue;
                }
                if (ranges_overlap(a->offset, a->size, b->offset, b->size)) {
                    if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                        "section overlap index=", i)) goto done;
                    if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, j,
                                        "section overlap peer=", j)) goto done;
                }
            }
        }
    }

    for (i = 0; i < obj->reloc_count; ++i) {
        struct elf_reloc *r = obj->relocs[i];
        int relsz;
        if (r == NULL || r->section == NULL) {
            if (report_diag(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i, "relocation has NULL section")) goto done;
            continue;
        }
        if (r->symbol == NULL) {
            if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                "relocation missing symbol index=", i)) goto done;
        } else if (r->symbol->obj != obj) {
            if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                "relocation symbol from different object index=", i)) goto done;
        }
        if (r->offset >= r->section->size && r->section->type != SHT_NOBITS) {
            if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                "relocation offset out of range idx=", i)) goto done;
        }
        relsz = elf_reloc_size_for_machine(obj->machine, r->type);
        if (relsz < 0) {
            if (report_diag_fmt(&ctx, ELF_DIAG_WARNING, ELF_ERR_UNSUPPORTED, i,
                                "unrecognized relocation type idx=", i)) {
                goto done;
            }
        }
        if (obj->machine == EM_MIPS && r->section != NULL) {
            if (obj->cls == ELFOBJ_CLASS_32 && r->has_addend) {
                if (report_diag(&ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, i,
                                "MIPS32 relocation should use REL format")) {
                    goto done;
                }
            }
            if (obj->cls == ELFOBJ_CLASS_64 && !r->has_addend) {
                if (report_diag(&ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, i,
                                "MIPS64 relocation should use RELA format")) {
                    goto done;
                }
            }
            if (obj->cls == ELFOBJ_CLASS_64 && r->type > 255u) {
                if (report_diag(&ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, i,
                                "MIPS64 compound relocation encoding detected")) {
                    goto done;
                }
            }
        }
        if (relsz > 0 && r->section->type != SHT_NOBITS) {
            uint64_t end;
            if (!elf__u64_add(r->offset, (uint64_t)relsz, &end) || end > r->section->size) {
                if (report_diag_fmt(&ctx, ELF_DIAG_WARNING, ELF_ERR_RELOC, i,
                                    "relocation width out of range idx=", i)) goto done;
            }
        }
        if (obj->machine == EM_AARCH64 && r->type == R_AARCH64_ADR_PREL_PG_HI21) {
            size_t k;
            int found_pair = 0;
            for (k = 0; k < obj->reloc_count; ++k) {
                const struct elf_reloc *q = obj->relocs[k];
                if (q == NULL || q == r || q->section != r->section || q->symbol != r->symbol) {
                    continue;
                }
                if (q->type == R_AARCH64_ADD_ABS_LO12_NC ||
                    q->type == R_AARCH64_LDST8_ABS_LO12_NC ||
                    q->type == R_AARCH64_LDST16_ABS_LO12_NC ||
                    q->type == R_AARCH64_LDST32_ABS_LO12_NC ||
                    q->type == R_AARCH64_LDST64_ABS_LO12_NC ||
                    q->type == R_AARCH64_LDST128_ABS_LO12_NC) {
                    found_pair = 1;
                    break;
                }
            }
            if (!found_pair) {
                if (report_diag_fmt(&ctx, ELF_DIAG_WARNING, ELF_ERR_FORMAT, i,
                                    "ADRP relocation has no matching LO12 pair idx=", i)) {
                    goto done;
                }
            }
        }
    }

    for (i = 0; i < obj->symbol_count; ++i) {
        struct elf_symbol *s = obj->symbols[i];
        if (s == NULL) {
            if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                "NULL symbol entry index=", i)) goto done;
            continue;
        }
        if (s->name == NULL) {
            if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                "symbol missing name index=", i)) goto done;
        }
        if (s->shndx != SHN_UNDEF && s->shndx != SHN_ABS && s->shndx != SHN_COMMON &&
            s->shndx > obj->section_count) {
            if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                "symbol shndx out of range index=", i)) goto done;
        }
        if (s->bind != STB_LOCAL) {
            break;
        }
    }
    for (; i < obj->symbol_count; ++i) {
        struct elf_symbol *s = obj->symbols[i];
        if (s != NULL && s->bind == STB_LOCAL) {
            if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                "local symbol appears after globals index=", i)) goto done;
        }
    }
    for (i = 0; i < obj->symbol_count; ++i) {
        struct elf_symbol *a = obj->symbols[i];
        if (a == NULL || a->name == NULL || a->name[0] == '\0') {
            continue;
        }
        if (a->bind == STB_LOCAL) {
            continue;
        }
        for (j = i + 1; j < obj->symbol_count; ++j) {
            struct elf_symbol *b = obj->symbols[j];
            if (b == NULL || b->name == NULL || b->name[0] == '\0') {
                continue;
            }
            if (b->bind == STB_LOCAL) {
                continue;
            }
            if (strcmp(a->name, b->name) == 0) {
                if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                    "duplicate global symbol index=", i)) goto done;
                if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, j,
                                    "duplicate global peer=", j)) goto done;
            }
        }
    }

    if (elf_find_section(obj, ".symtab") != NULL && elf_find_section(obj, ".strtab") == NULL) {
        if (report_diag(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, UINT64_MAX,
                        "symbol table exists without strtab")) goto done;
    }

    for (i = 0; i < obj->segment_count; ++i) {
        struct elf_segment *seg = obj->segments[i];
        if (seg == NULL) {
            if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                "NULL segment entry index=", i)) goto done;
            continue;
        }
        if (seg->align == 0 || (seg->align & (seg->align - 1)) != 0) {
            if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                "segment align invalid index=", i)) goto done;
        }
        for (j = 0; j < seg->section_count; ++j) {
            if (seg->section_indices[j] >= obj->section_count) {
                if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_FORMAT, i,
                                    "segment section index out of range seg=", i)) goto done;
            }
        }
    }

    if (has_layout) {
        for (i = 0; i < obj->segment_count; ++i) {
            uint64_t a_lo;
            uint64_t a_hi;
            struct elf_segment *a = obj->segments[i];
            if (a == NULL || !segment_range(a, obj, &a_lo, &a_hi)) {
                continue;
            }
            for (j = i + 1; j < obj->segment_count; ++j) {
                uint64_t b_lo;
                uint64_t b_hi;
                struct elf_segment *b = obj->segments[j];
                if (b == NULL || !segment_range(b, obj, &b_lo, &b_hi)) {
                    continue;
                }
                if (ranges_overlap(a_lo, a_hi - a_lo, b_lo, b_hi - b_lo)) {
                    if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                        "segment overlap index=", i)) goto done;
                    if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, j,
                                        "segment overlap peer=", j)) goto done;
                }
            }
        }
    }

    if (obj->phdr_count != 0) {
        for (i = 0; i < obj->phdr_count; ++i) {
            const struct elf_phdr *ph = &obj->phdrs[i];
            if (obj->image != NULL && ph->filesz > 0 &&
                !elf__bounds_ok((size_t)ph->offset, (size_t)ph->filesz, obj->image_size)) {
                if (report_diag_fmt(&ctx, ELF_DIAG_ERROR, ELF_ERR_BOUNDS, i,
                                    "program header out of file bounds index=", i)) goto done;
            }
        }
        if (has_layout) {
            for (i = 0; i < obj->section_count; ++i) {
                struct elf_section *s = obj->sections[i];
                int covered = 0;
                if (!section_is_allocated(s)) {
                    continue;
                }
                for (j = 0; j < obj->phdr_count; ++j) {
                    const struct elf_phdr *ph = &obj->phdrs[j];
                    uint64_t ph_end;
                    uint64_t s_end;
                    if (ph->type != PT_LOAD && ph->type != PT_DYNAMIC && ph->type != PT_TLS) {
                        continue;
                    }
                    if (!elf__u64_add(ph->offset, ph->filesz, &ph_end) ||
                        !elf__u64_add(s->offset, s->size, &s_end)) {
                        continue;
                    }
                    if (s->offset >= ph->offset && s_end <= ph_end) {
                        covered = 1;
                        break;
                    }
                }
                if (!covered) {
                    if (report_diag_fmt(&ctx, strictness_level(&ctx), ELF_ERR_FORMAT, i,
                                        "allocated section not covered by segment index=", i)) goto done;
                }
            }
        }
    }

done:
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
    return ctx.has_error ? ELF_ERR_FORMAT : ELF_OK;
}

elf_err_t elf_validate(elfobj_t *obj, char **diagnostics) {
    elf_validate_options_t opts;
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    opts.mode = obj->validate_mode;
    opts.max_errors = obj->validate_max_errors;
    return elf_validate_ex(obj, &opts, diagnostics);
}
