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

elf_err_t elf_validate_ex(elfobj_t *obj, const elf_validate_options_t *options, char **diagnostics) {
    size_t i;
    size_t j;
    int has_layout;
    validate_ctx_t ctx;
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
        if (relsz > 0 && r->section->type != SHT_NOBITS) {
            uint64_t end;
            if (!elf__u64_add(r->offset, (uint64_t)relsz, &end) || end > r->section->size) {
                if (report_diag_fmt(&ctx, ELF_DIAG_WARNING, ELF_ERR_RELOC, i,
                                    "relocation width out of range idx=", i)) goto done;
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
