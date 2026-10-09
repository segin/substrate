/*
 * ld_gc.c -- --gc-sections and identical code folding.
 */

#include "ld.h"

static int is_gc_candidate_section(const elf_section_t *sec) {
    const char *name;
    uint64_t flags;
    uint32_t type;

    if (sec == NULL) {
        return 0;
    }
    name = elf_section_name(sec);
    type = elf_section_type(sec);
    flags = elf_section_flags(sec);
    if ((flags & SHF_ALLOC) == 0) {
        return 0;
    }
    if (type == SHT_NULL || type == SHT_SYMTAB || type == SHT_STRTAB || type == SHT_REL || type == SHT_RELA) {
        return 0;
    }
    if (name != NULL &&
        (strcmp(name, ".shstrtab") == 0 || strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 ||
         strcmp(name, ".group") == 0)) {
        return 0;
    }
    return 1;
}

static int mark_live_section(uint8_t *live, size_t count, uint16_t shndx, int *changed) {
    size_t idx;

    if (live == NULL || changed == NULL) {
        return -1;
    }
    if (shndx == SHN_UNDEF || shndx == SHN_ABS || shndx == SHN_COMMON || shndx >= 0xff00) {
        return 0;
    }
    if (shndx == 0 || (size_t)(shndx - 1) >= count) {
        return -1;
    }
    idx = (size_t)(shndx - 1);
    if (!live[idx]) {
        live[idx] = 1;
        *changed = 1;
    }
    return 0;
}

static void mark_group_peers_live(elfobj_t *obj, uint8_t *live, size_t count, int *changed) {
    size_t i;

    for (i = 0; i < count; ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        if (sec == NULL || !live[i]) {
            continue;
        }
        if ((elf_section_flags(sec) & SHF_GROUP) == 0) {
            continue;
        }
        {
            size_t j;
            for (j = 0; j < count; ++j) {
                const elf_section_t *peer = elf_section_get(obj, j);
                if (peer == NULL || live[j] || !is_gc_candidate_section(peer)) {
                    continue;
                }
                if ((elf_section_flags(peer) & SHF_GROUP) != 0) {
                    live[j] = 1;
                    *changed = 1;
                }
            }
        }
    }
}

int gc_sections_by_reachability(elfobj_t *obj, const ld_ctx_t *ctx) {
    uint8_t *live;
    size_t count;
    int changed;
    size_t i;

    if (obj == NULL || ctx == NULL) {
        return -1;
    }
    count = elf_section_count(obj);
    live = (uint8_t *)calloc(count, sizeof(*live));
    if (live == NULL && count != 0) {
        return -1;
    }

    changed = 0;
    if (ctx->entry_symbol != NULL && ctx->entry_symbol[0] != '\0') {
        const elf_symbol_t *entry = elf_find_symbol(obj, ctx->entry_symbol);
        if (entry != NULL && mark_live_section(live, count, elf_symbol_shndx(entry), &changed) != 0) {
            free(live);
            return -1;
        }
    } else {
        const elf_symbol_t *entry = elf_find_symbol(obj, "_start");
        if (entry != NULL && mark_live_section(live, count, elf_symbol_shndx(entry), &changed) != 0) {
            free(live);
            return -1;
        }
    }
    for (i = 0; i < ctx->force_undefined.count; ++i) {
        const elf_symbol_t *root = elf_find_symbol(obj, ctx->force_undefined.items[i]);
        if (root != NULL && mark_live_section(live, count, elf_symbol_shndx(root), &changed) != 0) {
            free(live);
            return -1;
        }
    }
    for (i = 0; i < count; ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        uint32_t type = sec != NULL ? elf_section_type(sec) : SHT_NULL;
        if (type == SHT_INIT_ARRAY || type == SHT_FINI_ARRAY || type == SHT_PREINIT_ARRAY) {
            live[i] = 1;
            changed = 1;
            continue;
        }
        if (sec != NULL && (elf_section_flags(sec) & SHF_GNU_RETAIN) != 0) {
            live[i] = 1;
            changed = 1;
        }
    }

    do {
        changed = 0;
        for (i = 0; i < count; ++i) {
            elf_section_t *sec;
            size_t rc;
            size_t ri;
            if (!live[i]) {
                continue;
            }
            sec = elf_section_get(obj, i);
            if (sec == NULL) {
                continue;
            }
            rc = elf_section_reloc_count(sec);
            for (ri = 0; ri < rc; ++ri) {
                const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
                const elf_symbol_t *sym;
                if (rel == NULL) {
                    continue;
                }
                sym = elf_reloc_symbol(rel);
                if (sym == NULL) {
                    continue;
                }
                if (mark_live_section(live, count, elf_symbol_shndx(sym), &changed) != 0) {
                    free(live);
                    return -1;
                }
            }
        }
        mark_group_peers_live(obj, live, count, &changed);
    } while (changed);

    for (i = count; i > 0; --i) {
        size_t idx = i - 1;
        elf_section_t *sec = elf_section_get(obj, idx);
        if (sec == NULL || !is_gc_candidate_section(sec) || live[idx]) {
            continue;
        }
        if (ctx->gc_print_sections) {
            const char *name = elf_section_name(sec);
            fprintf(stderr, "ld: gc-sections: removing %s\n", name != NULL ? name : "<unnamed>");
        }
        if (elf_remove_section(obj, sec) != ELF_OK) {
            free(live);
            return -1;
        }
    }
    free(live);
    return 0;
}

static int is_icf_special_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, ".init_array") == 0 || strcmp(name, ".fini_array") == 0 ||
           strcmp(name, ".preinit_array") == 0 || strcmp(name, ".dynamic") == 0 ||
           strcmp(name, ".dynsym") == 0 || strcmp(name, ".dynstr") == 0;
}

static int is_icf_candidate_section(const elf_section_t *sec, int icf_mode) {
    uint64_t flags;
    uint32_t type;
    const char *name;

    if (sec == NULL || icf_mode == 0) {
        return 0;
    }
    type = elf_section_type(sec);
    flags = elf_section_flags(sec);
    name = elf_section_name(sec);
    if (type != SHT_PROGBITS || (flags & SHF_ALLOC) == 0) {
        return 0;
    }
    if ((flags & SHF_GROUP) != 0) {
        return 0;
    }
    if (is_icf_special_name(name)) {
        return 0;
    }
    if (icf_mode == 1 && (flags & SHF_WRITE) != 0) {
        return 0;
    }
    return 1;
}

static int sections_reloc_signature_equal(const elf_section_t *a, const elf_section_t *b) {
    size_t ra_n;
    size_t rb_n;
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }
    ra_n = elf_section_reloc_count(a);
    rb_n = elf_section_reloc_count(b);
    if (ra_n != rb_n) {
        return 0;
    }
    for (i = 0; i < ra_n; ++i) {
        const elf_reloc_t *ra = elf_section_reloc_at((elf_section_t *)a, i);
        const elf_reloc_t *rb = elf_section_reloc_at((elf_section_t *)b, i);
        const elf_symbol_t *sa;
        const elf_symbol_t *sb;
        const char *na = NULL;
        const char *nb = NULL;

        if (ra == NULL || rb == NULL) {
            return 0;
        }
        if (elf_reloc_offset(ra) != elf_reloc_offset(rb) || elf_reloc_type(ra) != elf_reloc_type(rb) ||
            elf_reloc_addend(ra) != elf_reloc_addend(rb) || elf_reloc_has_addend(ra) != elf_reloc_has_addend(rb)) {
            return 0;
        }
        sa = elf_reloc_symbol(ra);
        sb = elf_reloc_symbol(rb);
        na = sa != NULL ? elf_symbol_name(sa) : NULL;
        nb = sb != NULL ? elf_symbol_name(sb) : NULL;
        if ((na == NULL) != (nb == NULL)) {
            return 0;
        }
        if (na != NULL && strcmp(na, nb) != 0) {
            return 0;
        }
    }
    return 1;
}

static int sections_icf_equal(const elf_section_t *a, const elf_section_t *b) {
    size_t asz = 0;
    size_t bsz = 0;
    const void *ad;
    const void *bd;

    if (a == NULL || b == NULL) {
        return 0;
    }
    if (elf_section_type(a) != elf_section_type(b) || elf_section_flags(a) != elf_section_flags(b) ||
        elf_section_align(a) != elf_section_align(b) || elf_section_size(a) != elf_section_size(b)) {
        return 0;
    }
    ad = elf_section_data(a, &asz);
    bd = elf_section_data(b, &bsz);
    if (asz != bsz) {
        return 0;
    }
    if (asz != 0 && (ad == NULL || bd == NULL || memcmp(ad, bd, asz) != 0)) {
        return 0;
    }
    if (!sections_reloc_signature_equal(a, b)) {
        return 0;
    }
    return 1;
}

static int icf_fold_section(elfobj_t *obj, size_t leader_idx, size_t dupe_idx) {
    size_t i;
    elf_section_t *dupe;
    uint16_t dupe_shndx;
    uint16_t leader_shndx;

    if (obj == NULL || leader_idx >= elf_section_count(obj) || dupe_idx >= elf_section_count(obj)) {
        return -1;
    }
    dupe = elf_section_get(obj, dupe_idx);
    if (dupe == NULL) {
        return -1;
    }
    dupe_shndx = (uint16_t)(dupe_idx + 1);
    leader_shndx = (uint16_t)(leader_idx + 1);
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        elf_symbol_t *sym = elf_symbol_at(obj, i);
        if (sym == NULL) {
            continue;
        }
        if (elf_symbol_shndx(sym) == dupe_shndx) {
            if (elf_symbol_set_shndx(sym, leader_shndx) != ELF_OK) {
                return -1;
            }
        }
    }
    if (elf_remove_section(obj, dupe) != ELF_OK) {
        return -1;
    }
    return 0;
}

int apply_identical_code_folding(elfobj_t *obj, const ld_ctx_t *ctx) {
    size_t i;

    if (obj == NULL || ctx == NULL || ctx->icf_mode == 0) {
        return 0;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *leader = elf_section_get(obj, i);
        size_t j;

        if (!is_icf_candidate_section(leader, ctx->icf_mode)) {
            continue;
        }
        for (j = i + 1; j < elf_section_count(obj);) {
            elf_section_t *dupe = elf_section_get(obj, j);
            if (!is_icf_candidate_section(dupe, ctx->icf_mode) || !sections_icf_equal(leader, dupe)) {
                j++;
                continue;
            }
            if (icf_fold_section(obj, i, j) != 0) {
                return -1;
            }
        }
    }
    return 0;
}
