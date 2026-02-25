#include "elf_private.h"

static int ranges_overlap(uint64_t a_off, uint64_t a_sz, uint64_t b_off, uint64_t b_sz) {
    if (a_sz == 0 || b_sz == 0) {
        return 0;
    }
    return (a_off < b_off + b_sz) && (b_off < a_off + a_sz);
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
        end = s->offset + s->size;
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

elf_err_t elf_validate(elfobj_t *obj, char **diagnostics) {
    size_t i;
    size_t j;
    int has_error = 0;
    int has_layout = 0;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }

    free(obj->diag.buf);
    obj->diag.buf = NULL;
    obj->diag.len = 0;
    obj->diag.cap = 0;
    has_layout = (obj->image != NULL) || obj->finalized;

    for (i = 0; i < obj->section_count; ++i) {
        struct elf_section *s = obj->sections[i];
        if (s == NULL) {
            has_error = 1;
            (void)elf__append_diag(obj, "NULL section entry");
            continue;
        }
        if (s->type != SHT_NOBITS && obj->image != NULL && s->size > 0) {
            if (!elf__bounds_ok((size_t)s->offset, (size_t)s->size, obj->image_size)) {
                has_error = 1;
                (void)elf__append_diag_fmt(obj, "section out of file bounds index=", i);
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
                    has_error = 1;
                    (void)elf__append_diag_fmt(obj, "section overlap index=", i);
                    (void)elf__append_diag_fmt(obj, "section overlap peer=", j);
                }
            }
        }
    }

    for (i = 0; i < obj->reloc_count; ++i) {
        struct elf_reloc *r = obj->relocs[i];
        if (r == NULL || r->section == NULL) {
            has_error = 1;
            (void)elf__append_diag(obj, "relocation has NULL section");
            continue;
        }
        if (r->offset >= r->section->size && r->section->type != SHT_NOBITS) {
            has_error = 1;
            (void)elf__append_diag_fmt(obj, "relocation offset out of range idx=", i);
        }
    }

    for (i = 0; i < obj->segment_count; ++i) {
        struct elf_segment *seg = obj->segments[i];
        if (seg == NULL) {
            has_error = 1;
            (void)elf__append_diag_fmt(obj, "NULL segment entry index=", i);
            continue;
        }
        if (seg->align == 0 || (seg->align & (seg->align - 1)) != 0) {
            has_error = 1;
            (void)elf__append_diag_fmt(obj, "segment align invalid index=", i);
        }
        for (j = 0; j < seg->section_count; ++j) {
            if (seg->section_indices[j] >= obj->section_count) {
                has_error = 1;
                (void)elf__append_diag_fmt(obj, "segment section index out of range seg=", i);
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
                    has_error = 1;
                    (void)elf__append_diag_fmt(obj, "segment overlap index=", i);
                    (void)elf__append_diag_fmt(obj, "segment overlap peer=", j);
                }
            }
        }
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
