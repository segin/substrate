#include "elf_private.h"

static int is_internal_section_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 ||
           strcmp(name, ".shstrtab") == 0 || strncmp(name, ".rel", 4) == 0 ||
           strncmp(name, ".rela", 5) == 0;
}

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} comdat_set_t;

static int safe_str_has_nul(const uint8_t *data, size_t size, uint32_t off, const char **out_name) {
    size_t i;

    if (data == NULL || out_name == NULL || off >= size) {
        return 0;
    }
    for (i = off; i < size; ++i) {
        if (data[i] == '\0') {
            *out_name = (const char *)(data + off);
            return 1;
        }
    }
    return 0;
}

static void comdat_set_free(comdat_set_t *set) {
    size_t i;

    if (set == NULL) {
        return;
    }
    for (i = 0; i < set->count; ++i) {
        free(set->items[i]);
    }
    free(set->items);
    set->items = NULL;
    set->count = 0;
    set->cap = 0;
}

static int comdat_set_contains(const comdat_set_t *set, const char *name) {
    size_t i;

    if (set == NULL || name == NULL) {
        return 0;
    }
    for (i = 0; i < set->count; ++i) {
        if (strcmp(set->items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static elf_err_t comdat_set_add(comdat_set_t *set, const char *name) {
    char **next;

    if (set == NULL || name == NULL || name[0] == '\0' || comdat_set_contains(set, name)) {
        return ELF_OK;
    }
    if (set->count == set->cap) {
        size_t ncap = set->cap == 0 ? 16 : set->cap * 2;
        next = (char **)realloc(set->items, ncap * sizeof(*next));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        set->items = next;
        set->cap = ncap;
    }
    set->items[set->count] = elf__strdup(name);
    if (set->items[set->count] == NULL) {
        return ELF_ERR_OOM;
    }
    set->count++;
    return ELF_OK;
}

static char *group_signature_name(const elfobj_t *obj, const struct elf_section *group_sec) {
    const struct elf_section *symtab;
    const struct elf_section *strtab;
    const uint8_t *symp;
    uint32_t st_name = 0;
    const char *sig_name = NULL;
    size_t entsz;
    size_t nsyms;

    if (obj == NULL || group_sec == NULL || group_sec->type != SHT_GROUP) {
        return NULL;
    }
    if (group_sec->link >= obj->section_count) {
        return NULL;
    }
    symtab = obj->sections[group_sec->link];
    if (symtab == NULL || (symtab->type != SHT_SYMTAB && symtab->type != SHT_DYNSYM) || symtab->data == NULL) {
        return NULL;
    }
    entsz = (size_t)(symtab->entsize ? symtab->entsize : (obj->cls == ELFOBJ_CLASS_32 ? 16 : 24));
    if (entsz == 0 || symtab->data_size % entsz != 0) {
        return NULL;
    }
    nsyms = symtab->data_size / entsz;
    if (group_sec->info >= nsyms) {
        return NULL;
    }
    if (symtab->link >= obj->section_count) {
        return NULL;
    }
    strtab = obj->sections[symtab->link];
    if (strtab == NULL || strtab->type != SHT_STRTAB || strtab->data == NULL) {
        return NULL;
    }
    symp = symtab->data + (group_sec->info * entsz);
    st_name = elf__rd32(symp + 0, obj->endian);
    if (!safe_str_has_nul(strtab->data, strtab->data_size, st_name, &sig_name) ||
        sig_name == NULL || sig_name[0] == '\0') {
        return NULL;
    }
    return elf__strdup(sig_name);
}

static elf_err_t mark_discarded_comdat_members(const struct elf_link_input *input, comdat_set_t *seen,
                                                uint8_t *sec_discard) {
    size_t i;

    for (i = 0; i < input->obj->section_count; ++i) {
        struct elf_section *sec = input->obj->sections[i];
        const uint8_t *data;
        size_t words;
        uint32_t flags;
        char *sig = NULL;
        elf_err_t err;
        size_t w;

        if (sec == NULL || sec->type != SHT_GROUP || sec->data == NULL || sec->data_size < 4 ||
            (sec->data_size % 4) != 0) {
            continue;
        }
        data = sec->data;
        words = sec->data_size / 4;
        flags = elf__rd32(data, input->obj->endian);
        if ((flags & 0x1u) == 0) {
            continue;
        }
        sig = group_signature_name(input->obj, sec);
        if (sig == NULL || sig[0] == '\0') {
            free(sig);
            continue;
        }
        if (comdat_set_contains(seen, sig)) {
            sec_discard[i] = 1;
            for (w = 1; w < words; ++w) {
                uint32_t member = elf__rd32(data + (w * 4), input->obj->endian);
                if (member < input->obj->section_count) {
                    sec_discard[member] = 1;
                }
            }
            free(sig);
            continue;
        }
        err = comdat_set_add(seen, sig);
        free(sig);
        if (err != ELF_OK) {
            return err;
        }
    }
    return ELF_OK;
}

static int arm_float_conflict(uint32_t a, uint32_t b) {
    uint32_t ah = a & EF_ARM_ABI_FLOAT_HARD;
    uint32_t as = a & EF_ARM_ABI_FLOAT_SOFT;
    uint32_t bh = b & EF_ARM_ABI_FLOAT_HARD;
    uint32_t bs = b & EF_ARM_ABI_FLOAT_SOFT;
    return (ah && bs) || (as && bh);
}

static int parse_aarch64_feature_bits(const struct elf_section *sec, uint32_t *out_bits) {
    const uint8_t *p;
    size_t off;
    uint32_t namesz, descsz;

    if (sec == NULL || sec->data == NULL || sec->data_size < 16 || out_bits == NULL) {
        return 0;
    }
    p = sec->data;
    namesz = elf__rd32(p + 0, sec->obj->endian);
    descsz = elf__rd32(p + 4, sec->obj->endian);
    off = 12;
    off = (off + namesz + 3u) & ~3u;
    if (off + descsz > sec->data_size) {
        return 0;
    }
    while (off + 8 <= sec->data_size && descsz >= 8) {
        uint32_t t = elf__rd32(p + off, sec->obj->endian);
        uint32_t sz = elf__rd32(p + off + 4, sec->obj->endian);
        off += 8;
        if (off + sz > sec->data_size) {
            return 0;
        }
        if (t == GNU_PROPERTY_AARCH64_FEATURE_1_AND && sz >= 4) {
            *out_bits = elf__rd32(p + off, sec->obj->endian);
            return 1;
        }
        off += sz;
        off = (off + 7u) & ~7u;
        if (descsz < 8 + sz) {
            break;
        }
        descsz -= 8 + sz;
    }
    return 0;
}

static uint64_t ppc64_local_entry_offset(uint8_t other) {
    uint8_t v = (uint8_t)((other & STO_PPC64_LOCAL_MASK) >> STO_PPC64_LOCAL_BIT);
    uint64_t off = (uint64_t)1u << v;
    return (off >> 2) << 2;
}

static elf_err_t ppc64_validate_opd(const elfobj_t *obj) {
    const struct elf_section *opd = elf_find_section((elfobj_t *)obj, ".opd");
    if (opd == NULL) {
        return ELF_OK;
    }
    if (opd->size == 0) {
        return ELF_OK;
    }
    if (opd->entsize == 0 && (opd->size % 24u) != 0) {
        return ELF_ERR_FORMAT;
    }
    if (opd->entsize != 0 && opd->entsize != 24u) {
        return ELF_ERR_FORMAT;
    }
    return ELF_OK;
}

static elf_err_t merge_arch_metadata(elfobj_t *out, const elfobj_t *in) {
    if (out->machine == EM_ARM) {
        if (arm_float_conflict(out->flags, in->flags)) {
            elf__set_err(out, ELF_ERR_FORMAT, "ARM hard/soft-float ABI conflict");
            return ELF_ERR_FORMAT;
        }
        out->flags |= in->flags & (EF_ARM_ABI_FLOAT_HARD | EF_ARM_ABI_FLOAT_SOFT | EF_ARM_INTERWORK);
        if ((in->flags & 0xFF000000u) > (out->flags & 0xFF000000u)) {
            out->flags = (out->flags & ~0xFF000000u) | (in->flags & 0xFF000000u);
        }
        return ELF_OK;
    }
    if (out->machine == EM_AARCH64) {
        struct elf_section *dst = elf_find_section(out, ".note.gnu.property");
        struct elf_section *src = elf_find_section((elfobj_t *)in, ".note.gnu.property");
        uint32_t out_bits = GNU_PROPERTY_AARCH64_FEATURE_1_BTI | GNU_PROPERTY_AARCH64_FEATURE_1_PAC;
        uint32_t in_bits = out_bits;
        if (src == NULL) {
            return ELF_OK;
        }
        if (dst != NULL) {
            (void)parse_aarch64_feature_bits(dst, &out_bits);
        }
        (void)parse_aarch64_feature_bits(src, &in_bits);
        out_bits &= in_bits;
        if (elf_add_gnu_property_aarch64(out, out_bits) != ELF_OK) {
            return ELF_ERR_OOM;
        }
    }
    if (out->machine == EM_MIPS) {
        elf_mips_abiflags_t a;
        elf_mips_abiflags_t b;
        int ha = elf_mips_abiflags(out, &a);
        int hb = elf_mips_abiflags(in, &b);
        if (ha && hb && (a.isa_level != b.isa_level || a.fp_abi != b.fp_abi ||
                         a.gpr_size != b.gpr_size || a.cpr1_size != b.cpr1_size)) {
            elf__set_err(out, ELF_ERR_FORMAT, "MIPS ABIFLAGS conflict across link inputs");
            return ELF_ERR_FORMAT;
        }
    }
    if (out->machine == EM_RISCV) {
        uint32_t a = out->flags & EF_RISCV_FLOAT_ABI_QUAD;
        uint32_t b = in->flags & EF_RISCV_FLOAT_ABI_QUAD;
        if (a != b) {
            elf__set_err(out, ELF_ERR_FORMAT, "RISC-V float ABI conflict across link inputs");
            return ELF_ERR_FORMAT;
        }
        out->flags |= in->flags & (EF_RISCV_RVC | EF_RISCV_RVE | EF_RISCV_TSO);
    }
    if (out->machine == EM_PPC64) {
        uint32_t out_abi = out->flags & EF_PPC64_ABI;
        uint32_t in_abi = in->flags & EF_PPC64_ABI;
        if (out_abi != 0 && in_abi != 0 && out_abi != in_abi) {
            elf__set_err(out, ELF_ERR_FORMAT, "PPC64 ELFv1/ELFv2 ABI conflict across link inputs");
            return ELF_ERR_FORMAT;
        }
        if (out_abi == 0) {
            out->flags = (out->flags & ~EF_PPC64_ABI) | in_abi;
            out_abi = in_abi;
        }
        if (in_abi == EF_PPC64_ABI_V1 && ppc64_validate_opd(in) != ELF_OK) {
            elf__set_err(out, ELF_ERR_FORMAT, "invalid PPC64 .opd function descriptor section");
            return ELF_ERR_FORMAT;
        }
        if (out_abi == EF_PPC64_ABI_V1 && ppc64_validate_opd(out) != ELF_OK) {
            elf__set_err(out, ELF_ERR_FORMAT, "invalid PPC64 .opd function descriptor section");
            return ELF_ERR_FORMAT;
        }
        if (out_abi == EF_PPC64_ABI_V2) {
            const struct elf_section *toc = elf_find_section(out, ".toc");
            if (toc == NULL) {
                toc = elf_find_section(out, ".got");
            }
            if (toc != NULL) {
                (void)elf__diag_append(out, ELF_DIAG_INFO, ELF_OK, UINT64_MAX,
                                       "PPC64 ELFv2 TOC base derived from .toc/.got");
            }
        }
    }
    return ELF_OK;
}

static elf_err_t append_section_data(struct elf_section *dst, const uint8_t *src, size_t src_sz,
                                     uint64_t *base_out) {
    uint8_t *buf;
    uint64_t base = 0;

    if (base_out != NULL) {
        *base_out = 0;
    }
    if (dst == NULL) {
        return ELF_ERR_STATE;
    }
    base = dst->type == SHT_NOBITS ? dst->size : dst->data_size;
    if (base_out != NULL) {
        *base_out = base;
    }

    if (src_sz == 0) {
        return ELF_OK;
    }
    if (dst->type == SHT_NOBITS) {
        dst->size += src_sz;
        return ELF_OK;
    }

    buf = (uint8_t *)realloc(dst->data, dst->data_size + src_sz);
    if (buf == NULL) {
        return ELF_ERR_OOM;
    }
    memcpy(buf + dst->data_size, src, src_sz);
    dst->data = buf;
    dst->owns_data = 1;
    dst->data_size += src_sz;
    dst->size = dst->data_size;
    return ELF_OK;
}

static elf_err_t replace_section_data(struct elf_section *dst, const struct elf_section *src) {
    uint8_t *copy = NULL;

    if (dst == NULL || src == NULL) {
        return ELF_ERR_STATE;
    }

    if (dst->owns_data && dst->data != NULL) {
        free(dst->data);
    }
    dst->data = NULL;
    dst->data_size = 0;
    dst->owns_data = 0;
    dst->size = 0;

    dst->type = src->type;
    dst->flags = src->flags;
    dst->addralign = src->addralign;
    dst->entsize = src->entsize;

    if (src->type == SHT_NOBITS) {
        dst->size = src->size;
        return ELF_OK;
    }
    if (src->data_size == 0 || src->data == NULL) {
        dst->size = 0;
        return ELF_OK;
    }

    copy = (uint8_t *)malloc(src->data_size);
    if (copy == NULL) {
        return ELF_ERR_OOM;
    }
    memcpy(copy, src->data, src->data_size);
    dst->data = copy;
    dst->data_size = src->data_size;
    dst->size = src->data_size;
    dst->owns_data = 1;
    return ELF_OK;
}

static elf_err_t plan_push_input(elf_link_plan_t *plan, elfobj_t *obj, const char *name) {
    void *next;
    struct elf_link_input *in;

    if (plan->input_count == plan->input_cap) {
        size_t new_cap = plan->input_cap == 0 ? 8 : plan->input_cap * 2;
        next = elf__reallocarray(plan->inputs, new_cap, sizeof(plan->inputs[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        plan->inputs = (struct elf_link_input *)next;
        plan->input_cap = new_cap;
    }

    in = &plan->inputs[plan->input_count++];
    memset(in, 0, sizeof(*in));
    in->obj = obj;
    in->name = elf__strdup(name == NULL ? "" : name);
    if (in->name == NULL) {
        return ELF_ERR_OOM;
    }
    return ELF_OK;
}

static void free_map_entries(elf_link_plan_t *plan) {
    size_t i;

    for (i = 0; i < plan->map_count; ++i) {
        free(plan->map_entries[i].symbol_name);
        free(plan->map_entries[i].section_name);
        free(plan->map_entries[i].input_name);
    }
    free(plan->map_entries);
    plan->map_entries = NULL;
    plan->map_count = 0;
    plan->map_cap = 0;
}

static elf_err_t plan_push_map_entry(elf_link_plan_t *plan, const char *symbol_name,
                                     const char *section_name, const char *input_name,
                                     uint64_t value, size_t input_index) {
    void *next;
    struct elf_link_map_entry_rec *rec;

    if (plan->map_count == plan->map_cap) {
        size_t new_cap = plan->map_cap == 0 ? 16 : plan->map_cap * 2;
        next = elf__reallocarray(plan->map_entries, new_cap, sizeof(plan->map_entries[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        plan->map_entries = (struct elf_link_map_entry_rec *)next;
        plan->map_cap = new_cap;
    }
    rec = &plan->map_entries[plan->map_count++];
    memset(rec, 0, sizeof(*rec));
    rec->symbol_name = elf__strdup(symbol_name == NULL ? "" : symbol_name);
    rec->section_name = elf__strdup(section_name == NULL ? "" : section_name);
    rec->input_name = elf__strdup(input_name == NULL ? "" : input_name);
    if (rec->symbol_name == NULL || rec->section_name == NULL || rec->input_name == NULL) {
        free(rec->symbol_name);
        free(rec->section_name);
        free(rec->input_name);
        memset(rec, 0, sizeof(*rec));
        plan->map_count--;
        return ELF_ERR_OOM;
    }
    rec->value = value;
    rec->input_index = input_index;
    return ELF_OK;
}

static elf_err_t merge_sections(elf_link_plan_t *plan, elfobj_t *out,
                                const struct elf_link_input *input,
                                uint64_t *sec_bases, uint8_t *sec_included,
                                comdat_set_t *comdat_seen) {
    size_t j;
    uint8_t *sec_discard = NULL;
    elf_err_t pre_err;

    sec_discard = (uint8_t *)elf__calloc(input->obj->section_count, sizeof(sec_discard[0]));
    if (sec_discard == NULL && input->obj->section_count != 0) {
        return ELF_ERR_OOM;
    }
    pre_err = mark_discarded_comdat_members(input, comdat_seen, sec_discard);
    if (pre_err != ELF_OK) {
        free(sec_discard);
        return pre_err;
    }

    for (j = 0; j < input->obj->section_count; ++j) {
        struct elf_section *src = input->obj->sections[j];
        struct elf_section *dst;
        elf_link_merge_action_t action = ELF_LINK_MERGE_APPEND;
        elf_err_t err;

        if (src == NULL || src->name == NULL || src->name[0] == '\0') {
            continue;
        }
        if (src->type == SHT_GROUP || sec_discard[j]) {
            continue;
        }
        if (is_internal_section_name(src->name)) {
            continue;
        }
        if (plan->gc_hook != NULL && !plan->gc_hook(src, plan->gc_user)) {
            continue;
        }

        dst = elf_find_section(out, src->name);
        if (dst != NULL && plan->section_merge_hook != NULL) {
            action = plan->section_merge_hook(src->name, dst, src, plan->section_merge_user);
        }
        if (action == ELF_LINK_MERGE_SKIP) {
            continue;
        }

        if (dst == NULL) {
            dst = elf_add_section(out, src->name, src->type, src->flags);
            if (dst == NULL) {
                free(sec_discard);
                return out->last_err == ELF_OK ? ELF_ERR_OOM : out->last_err;
            }
            dst->addralign = src->addralign;
        } else if (strcmp(src->name, ".ARM.attributes") == 0 && src->data_size != 0 &&
                   dst->data_size != 0 &&
                   (src->data_size != dst->data_size ||
                    memcmp(src->data, dst->data, src->data_size) != 0)) {
            elf__set_err(out, ELF_ERR_FORMAT, ".ARM.attributes mismatch across inputs");
            free(sec_discard);
            return ELF_ERR_FORMAT;
        } else if (action == ELF_LINK_MERGE_APPEND) {
            if (dst->type != src->type) {
                elf__set_err(out, ELF_ERR_FORMAT, "section type mismatch during merge");
                (void)elf__append_diag(out, src->name);
                free(sec_discard);
                return ELF_ERR_FORMAT;
            }
            if (src->addralign > dst->addralign) {
                dst->addralign = src->addralign;
            }
            dst->flags |= src->flags;
            if (src->entsize != 0) {
                if (dst->entsize == 0) {
                    dst->entsize = src->entsize;
                } else if (dst->entsize != src->entsize) {
                    elf__set_err(out, ELF_ERR_FORMAT, "section entsize mismatch during merge");
                    (void)elf__append_diag(out, src->name);
                    free(sec_discard);
                    return ELF_ERR_FORMAT;
                }
            }
        }

        if (action == ELF_LINK_MERGE_REPLACE) {
            err = replace_section_data(dst, src);
            if (err != ELF_OK) {
                free(sec_discard);
                return err;
            }
            sec_bases[j] = 0;
        } else {
            err = append_section_data(dst, src->data, src->data_size, &sec_bases[j]);
            if (err != ELF_OK) {
                free(sec_discard);
                return err;
            }
            if (src->type == SHT_NOBITS && src->size > 0) {
                uint64_t old_sz = dst->size;
                dst->size += src->size;
                sec_bases[j] = old_sz;
            }
        }
        sec_included[j] = 1;
    }

    free(sec_discard);
    return ELF_OK;
}

static int resolve_src_sec_index(const elfobj_t *obj, uint16_t shndx, size_t *out_idx) {
    size_t idx;
    struct elf_section *sec;
    int parsed_with_null0;

    if (obj == NULL || out_idx == NULL) {
        return 0;
    }
    if (shndx == SHN_UNDEF || shndx == SHN_ABS || shndx == SHN_COMMON) {
        return 0;
    }
    if (shndx == 0) {
        return 0;
    }
    parsed_with_null0 = obj->section_count > 0 && obj->sections[0] != NULL && obj->sections[0]->type == SHT_NULL;

    if (parsed_with_null0) {
        if ((size_t)shndx < obj->section_count) {
            sec = obj->sections[shndx];
            if (sec != NULL && sec->name != NULL && sec->name[0] != '\0') {
                *out_idx = (size_t)shndx;
                return 1;
            }
        }
        idx = (size_t)(shndx - 1);
        if (idx < obj->section_count) {
            sec = obj->sections[idx];
            if (sec != NULL && sec->name != NULL && sec->name[0] != '\0') {
                *out_idx = idx;
                return 1;
            }
        }
        return 0;
    }

    idx = (size_t)(shndx - 1);
    if (idx < obj->section_count) {
        sec = obj->sections[idx];
        if (sec != NULL && sec->name != NULL && sec->name[0] != '\0') {
            *out_idx = idx;
            return 1;
        }
    }
    if ((size_t)shndx < obj->section_count) {
        sec = obj->sections[shndx];
        if (sec != NULL && sec->name != NULL && sec->name[0] != '\0') {
            *out_idx = (size_t)shndx;
            return 1;
        }
    }
    return 0;
}

static elf_err_t merge_symbols(elf_link_plan_t *plan, elfobj_t *out,
                               const struct elf_link_input *input,
                               const uint64_t *sec_bases, const uint8_t *sec_included,
                               struct elf_symbol **sym_map,
                               size_t input_index) {
    size_t i;

    for (i = 0; i < input->obj->symbol_count; ++i) {
        struct elf_symbol *sym = input->obj->symbols[i];
        struct elf_symbol *existing;
        struct elf_symbol *n;
        uint64_t value = 0;
        uint16_t shndx = SHN_UNDEF;
        const char *sec_name = "";

        if (sym == NULL) {
            continue;
        }
        if (sym_map != NULL && i < input->obj->symbol_count) {
            sym_map[i] = NULL;
        }
        if (sym->name == NULL || sym->name[0] == '\0') {
            continue;
        }
        if (plan->version_hook != NULL &&
            !plan->version_hook(sym->name, NULL, plan->version_user)) {
            continue;
        }

        value = sym->value;
        shndx = sym->shndx;
        {
            size_t src_sec_index = 0;
            if (resolve_src_sec_index(input->obj, sym->shndx, &src_sec_index)) {
                struct elf_section *src_sec = input->obj->sections[src_sec_index];
                struct elf_section *dst_sec;

                if (!sec_included[src_sec_index]) {
                    continue;
                }
                dst_sec = elf_find_section(out, src_sec->name);
                if (dst_sec == NULL) {
                    continue;
                }
                if (!elf__u64_add(sym->value, sec_bases[src_sec_index], &value)) {
                    return ELF_ERR_BOUNDS;
                }
                shndx = (uint16_t)(dst_sec->index + 1);
                sec_name = dst_sec->name != NULL ? dst_sec->name : "";
            }
        }
        if (out->machine == EM_PPC64 && (out->flags & EF_PPC64_ABI) == EF_PPC64_ABI_V2 &&
            sym->type == STT_FUNC) {
            uint64_t le_off = ppc64_local_entry_offset(sym->other);
            if (!elf__u64_add(value, le_off, &value)) {
                return ELF_ERR_BOUNDS;
            }
        }

        if (sym->bind == STB_LOCAL) {
            n = elf_add_symbol(out, sym->name, value, sym->size, sym->bind, sym->type);
            if (n == NULL) {
                return out->last_err == ELF_OK ? ELF_ERR_OOM : out->last_err;
            }
            n->other = sym->other;
            n->shndx = shndx;
            n->ver_index = sym->ver_index;
            if (sym_map != NULL && i < input->obj->symbol_count) {
                sym_map[i] = n;
            }
            if (plan_push_map_entry(plan, n->name, sec_name, input->name, n->value, input_index) != ELF_OK) {
                return ELF_ERR_OOM;
            }
            continue;
        }

        existing = elf_find_symbol(out, sym->name);
        if (existing == NULL) {
            n = elf_add_symbol(out, sym->name, value, sym->size, sym->bind, sym->type);
            if (n == NULL) {
                return out->last_err == ELF_OK ? ELF_ERR_OOM : out->last_err;
            }
            n->other = sym->other;
            n->shndx = shndx;
            n->ver_index = sym->ver_index;
            if (sym_map != NULL && i < input->obj->symbol_count) {
                sym_map[i] = n;
            }
            if (plan_push_map_entry(plan, n->name, sec_name, input->name, n->value, input_index) != ELF_OK) {
                return ELF_ERR_OOM;
            }
            continue;
        }

        if (sym_map != NULL && i < input->obj->symbol_count) {
            sym_map[i] = existing;
        }

        if (existing->shndx == SHN_UNDEF && shndx != SHN_UNDEF) {
            existing->bind = sym->bind;
            existing->type = sym->type;
            existing->value = value;
            existing->size = sym->size;
            existing->shndx = shndx;
            existing->ver_index = sym->ver_index;
            continue;
        }

        if (existing->bind == STB_WEAK && sym->bind == STB_GLOBAL) {
            existing->bind = sym->bind;
            existing->type = sym->type;
            existing->value = value;
            existing->size = sym->size;
            existing->shndx = shndx;
            existing->ver_index = sym->ver_index;
        }
    }

    return ELF_OK;
}

static elf_err_t merge_relocations(elf_link_plan_t *plan, elfobj_t *out,
                                   const struct elf_link_input *input,
                                   const uint64_t *sec_bases, const uint8_t *sec_included,
                                   struct elf_symbol **sym_map,
                                   size_t input_index) {
    size_t i;
    (void)plan;

    for (i = 0; i < input->obj->reloc_count; ++i) {
        struct elf_reloc *r = input->obj->relocs[i];
        struct elf_section *dst_sec;
        struct elf_symbol *dst_sym;
        uint64_t off;
        size_t src_sec_index;

        if (r == NULL || r->section == NULL || r->section->name == NULL) {
            continue;
        }
        src_sec_index = r->section->index;
        if (src_sec_index >= input->obj->section_count || !sec_included[src_sec_index]) {
            continue;
        }
        dst_sec = elf_find_section(out, r->section->name);
        if (dst_sec == NULL) {
            continue;
        }
        if (!elf__u64_add(r->offset, sec_bases[src_sec_index], &off)) {
            return ELF_ERR_BOUNDS;
        }

        if (r->symbol == NULL) {
            continue;
        }
        if (sym_map != NULL && r->symbol->index < input->obj->symbol_count) {
            dst_sym = sym_map[r->symbol->index];
            if (dst_sym != NULL) {
                if (elf_add_relocation(dst_sec, off, dst_sym, r->type, r->addend) != ELF_OK) {
                    return ELF_ERR_RELOC;
                }
                continue;
            }
        }
        if (r->symbol->name == NULL || r->symbol->name[0] == '\0') {
            char anon_name[96];
            uint64_t anon_value = r->symbol->value;
            uint16_t anon_shndx = r->symbol->shndx;
            uint8_t anon_type = STT_NOTYPE;
            size_t sym_src_sec = 0;

            if (resolve_src_sec_index(input->obj, r->symbol->shndx, &sym_src_sec)) {
                struct elf_section *src_sym_sec = input->obj->sections[sym_src_sec];
                struct elf_section *dst_sym_sec;

                if (src_sym_sec == NULL || !sec_included[sym_src_sec]) {
                    continue;
                }
                dst_sym_sec = elf_find_section(out, src_sym_sec->name);
                if (dst_sym_sec == NULL) {
                    continue;
                }
                if (!elf__u64_add(r->symbol->value, sec_bases[sym_src_sec], &anon_value)) {
                    return ELF_ERR_BOUNDS;
                }
                anon_shndx = (uint16_t)(dst_sym_sec->index + 1);
                anon_type = STT_SECTION;
            }

            if (snprintf(anon_name, sizeof(anon_name), "__elfobj_reloc_%zu_%zu", input_index, i) < 0) {
                return ELF_ERR_STATE;
            }
            dst_sym = elf_add_symbol(out, anon_name, anon_value, 0, STB_LOCAL, anon_type);
            if (dst_sym == NULL) {
                return out->last_err == ELF_OK ? ELF_ERR_OOM : out->last_err;
            }
            dst_sym->shndx = anon_shndx;
        } else {
            dst_sym = elf_find_symbol(out, r->symbol->name);
            if (dst_sym == NULL) {
                elf__set_err(out, ELF_ERR_RELOC, "unresolved relocation symbol during link");
                (void)elf__append_diag(out, r->symbol->name);
                return ELF_ERR_RELOC;
            }
        }
        if (elf_add_relocation(dst_sec, off, dst_sym, r->type, r->addend) != ELF_OK) {
            return ELF_ERR_RELOC;
        }
    }

    return ELF_OK;
}

elf_link_plan_t *elf_link_plan_create(void) {
    return (elf_link_plan_t *)elf__calloc(1, sizeof(elf_link_plan_t));
}

void elf_link_plan_destroy(elf_link_plan_t *plan) {
    size_t i;

    if (plan == NULL) {
        return;
    }
    for (i = 0; i < plan->input_count; ++i) {
        free(plan->inputs[i].name);
    }
    free(plan->inputs);
    free_map_entries(plan);
    free(plan);
}

elf_err_t elf_link_plan_add_input(elf_link_plan_t *plan, elfobj_t *obj, const char *name) {
    if (plan == NULL || obj == NULL) {
        return ELF_ERR_STATE;
    }
    return plan_push_input(plan, obj, name);
}

size_t elf_link_plan_input_count(const elf_link_plan_t *plan) {
    return plan == NULL ? 0 : plan->input_count;
}

elf_err_t elf_link_plan_set_section_merge_hook(elf_link_plan_t *plan,
                                               elf_link_section_merge_hook_t hook,
                                               void *user) {
    if (plan == NULL) {
        return ELF_ERR_STATE;
    }
    plan->section_merge_hook = hook;
    plan->section_merge_user = user;
    return ELF_OK;
}

elf_err_t elf_link_plan_set_archive_hook(elf_link_plan_t *plan, elf_link_archive_hook_t hook,
                                         void *user) {
    if (plan == NULL) {
        return ELF_ERR_STATE;
    }
    plan->archive_hook = hook;
    plan->archive_user = user;
    return ELF_OK;
}

elf_err_t elf_link_plan_set_gc_hook(elf_link_plan_t *plan, elf_link_gc_hook_t hook, void *user) {
    if (plan == NULL) {
        return ELF_ERR_STATE;
    }
    plan->gc_hook = hook;
    plan->gc_user = user;
    return ELF_OK;
}

elf_err_t elf_link_plan_set_incremental_hook(elf_link_plan_t *plan,
                                             elf_link_incremental_hook_t hook, void *user) {
    if (plan == NULL) {
        return ELF_ERR_STATE;
    }
    plan->incremental_hook = hook;
    plan->incremental_user = user;
    return ELF_OK;
}

elf_err_t elf_link_plan_set_version_hook(elf_link_plan_t *plan, elf_link_version_hook_t hook,
                                         void *user) {
    if (plan == NULL) {
        return ELF_ERR_STATE;
    }
    plan->version_hook = hook;
    plan->version_user = user;
    return ELF_OK;
}

elf_err_t elf_link_plan_consider_archive_member(elf_link_plan_t *plan, const char *archive_path,
                                                const char *member_name, int *should_extract_out) {
    int extract;

    if (plan == NULL || should_extract_out == NULL) {
        return ELF_ERR_STATE;
    }
    extract = 1;
    if (plan->archive_hook != NULL) {
        extract = plan->archive_hook(archive_path, member_name, plan->archive_user) ? 1 : 0;
    }
    *should_extract_out = extract;
    return ELF_OK;
}

elf_err_t elf_link_plan_note_incremental(elf_link_plan_t *plan, const char *key, const char *value) {
    if (plan == NULL) {
        return ELF_ERR_STATE;
    }
    if (plan->incremental_hook != NULL) {
        plan->incremental_hook(key == NULL ? "" : key, value == NULL ? "" : value,
                               plan->incremental_user);
    }
    return ELF_OK;
}

elf_err_t elf_link_plan_link(elf_link_plan_t *plan, elfobj_t **output) {
    elfobj_t *out;
    comdat_set_t comdat_seen;
    size_t i;

    if (plan == NULL || output == NULL || plan->input_count == 0) {
        return ELF_ERR_STATE;
    }

    out = elf_create(ET_REL, plan->inputs[0].obj->machine, plan->inputs[0].obj->cls,
                     plan->inputs[0].obj->endian);
    if (out == NULL) {
        return ELF_ERR_OOM;
    }
    memset(&comdat_seen, 0, sizeof(comdat_seen));

    free_map_entries(plan);
    out->flags = plan->inputs[0].obj->flags;

    for (i = 0; i < plan->input_count; ++i) {
        const struct elf_link_input *in = &plan->inputs[i];
        uint64_t *sec_bases;
        uint8_t *sec_included;
        struct elf_symbol **sym_map;
        elf_err_t err;

        if (in->obj == NULL) {
            elf_close(out);
            return ELF_ERR_STATE;
        }
        if (in->obj->machine != out->machine || in->obj->cls != out->cls ||
            in->obj->endian != out->endian) {
            elf_close(out);
            return ELF_ERR_UNSUPPORTED;
        }
        err = merge_arch_metadata(out, in->obj);
        if (err != ELF_OK) {
            elf_close(out);
            return err;
        }

        sec_bases = (uint64_t *)elf__calloc(in->obj->section_count, sizeof(sec_bases[0]));
        sec_included = (uint8_t *)elf__calloc(in->obj->section_count, sizeof(sec_included[0]));
        sym_map = (struct elf_symbol **)elf__calloc(in->obj->symbol_count, sizeof(sym_map[0]));
        if ((sec_bases == NULL || sec_included == NULL || sym_map == NULL) &&
            (in->obj->section_count != 0 || in->obj->symbol_count != 0)) {
            free(sec_bases);
            free(sec_included);
            free(sym_map);
            elf_close(out);
            return ELF_ERR_OOM;
        }

        err = merge_sections(plan, out, in, sec_bases, sec_included, &comdat_seen);
        if (err == ELF_OK) {
            err = merge_symbols(plan, out, in, sec_bases, sec_included, sym_map, i);
        }
        if (err == ELF_OK) {
            err = merge_relocations(plan, out, in, sec_bases, sec_included, sym_map, i);
        }
        free(sec_bases);
        free(sec_included);
        free(sym_map);
        if (err != ELF_OK) {
            comdat_set_free(&comdat_seen);
            elf_close(out);
            return err;
        }
        (void)elf_link_plan_note_incremental(plan, "merged-input",
                                             in->name != NULL ? in->name : "");
    }

    comdat_set_free(&comdat_seen);
    *output = out;
    return ELF_OK;
}

size_t elf_link_plan_map_count(const elf_link_plan_t *plan) {
    return plan == NULL ? 0 : plan->map_count;
}

int elf_link_plan_map_entry(const elf_link_plan_t *plan, size_t index,
                            elf_link_map_entry_t *out_entry) {
    const struct elf_link_map_entry_rec *rec;

    if (plan == NULL || out_entry == NULL || index >= plan->map_count) {
        return 0;
    }
    rec = &plan->map_entries[index];
    out_entry->symbol_name = rec->symbol_name;
    out_entry->section_name = rec->section_name;
    out_entry->input_name = rec->input_name;
    out_entry->value = rec->value;
    out_entry->input_index = rec->input_index;
    return 1;
}

elf_err_t elf_link_load_objects(const char **paths, size_t count, elfobj_t ***out_objs,
                                size_t *out_count) {
    elfobj_t **objs;
    size_t i;

    if (paths == NULL || out_objs == NULL || out_count == NULL) {
        return ELF_ERR_STATE;
    }
    objs = (elfobj_t **)elf__calloc(count, sizeof(objs[0]));
    if (objs == NULL && count != 0) {
        return ELF_ERR_OOM;
    }
    for (i = 0; i < count; ++i) {
        if (elf_open(paths[i], &objs[i]) != ELF_OK) {
            size_t j;
            for (j = 0; j < i; ++j) {
                elf_close(objs[j]);
            }
            free(objs);
            return ELF_ERR_IO;
        }
    }
    *out_objs = objs;
    *out_count = count;
    return ELF_OK;
}

void elf_link_unload_objects(elfobj_t **objs, size_t count) {
    size_t i;

    if (objs == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        if (objs[i] != NULL) {
            elf_close(objs[i]);
        }
    }
    free(objs);
}

elf_symbol_t *elf_link_resolve_symbol(elfobj_t **inputs, size_t count, const char *name,
                                      size_t *input_index_out) {
    elf_symbol_t *best = NULL;
    size_t best_idx = 0;
    int best_rank = -1;
    size_t i;
    size_t j;

    if (inputs == NULL || name == NULL) {
        return NULL;
    }

    for (i = 0; i < count; ++i) {
        elfobj_t *obj = inputs[i];
        if (obj == NULL) {
            continue;
        }
        for (j = 0; j < obj->symbol_count; ++j) {
            struct elf_symbol *sym = obj->symbols[j];
            int rank;
            if (sym == NULL || sym->name == NULL || strcmp(sym->name, name) != 0) {
                continue;
            }
            rank = 0;
            if (sym->bind == STB_GLOBAL && sym->shndx != SHN_UNDEF) {
                rank = 4;
            } else if (sym->bind == STB_WEAK && sym->shndx != SHN_UNDEF) {
                rank = 3;
            } else if (sym->bind == STB_GLOBAL) {
                rank = 2;
            } else if (sym->bind == STB_WEAK) {
                rank = 1;
            }
            if (rank > best_rank) {
                best = sym;
                best_idx = i;
                best_rank = rank;
            }
        }
    }

    if (best != NULL && input_index_out != NULL) {
        *input_index_out = best_idx;
    }
    return best;
}

elf_section_t *elf_link_add_got_section(elfobj_t *obj, size_t entries) {
    elf_section_t *got;
    size_t i;
    size_t add_sz;
    uint8_t *zeros;
    elf_err_t err;

    if (obj == NULL) {
        return NULL;
    }
    got = elf_find_section(obj, ".got");
    if (got == NULL) {
        got = elf_add_section(obj, ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
        if (got == NULL) {
            return NULL;
        }
        got->addralign = obj->cls == ELFOBJ_CLASS_64 ? 8 : 4;
    }

    add_sz = entries * (obj->cls == ELFOBJ_CLASS_64 ? 8 : 4);
    if (add_sz == 0) {
        return got;
    }
    zeros = (uint8_t *)malloc(add_sz);
    if (zeros == NULL) {
        return NULL;
    }
    for (i = 0; i < add_sz; ++i) {
        zeros[i] = 0;
    }
    err = append_section_data(got, zeros, add_sz, NULL);
    free(zeros);
    if (err != ELF_OK) {
        return NULL;
    }
    return got;
}

elf_section_t *elf_link_add_plt_section(elfobj_t *obj, size_t entries) {
    elf_section_t *plt;
    size_t i;
    size_t add_sz;
    uint8_t *nops;
    elf_err_t err;

    if (obj == NULL) {
        return NULL;
    }
    plt = elf_find_section(obj, ".plt");
    if (plt == NULL) {
        plt = elf_add_section(obj, ".plt", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
        if (plt == NULL) {
            return NULL;
        }
        plt->addralign = 16;
    }

    add_sz = entries * 16;
    if (add_sz == 0) {
        return plt;
    }
    nops = (uint8_t *)malloc(add_sz);
    if (nops == NULL) {
        return NULL;
    }
    for (i = 0; i < add_sz; ++i) {
        nops[i] = 0x90;
    }
    err = append_section_data(plt, nops, add_sz, NULL);
    free(nops);
    if (err != ELF_OK) {
        return NULL;
    }
    return plt;
}

elf_err_t elf_link_add_dynamic_entry(elfobj_t *obj, int64_t tag, uint64_t value) {
    elf_section_t *dyn;
    uint8_t entry[16];
    size_t entsz;
    elf_err_t err;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    dyn = elf_find_section(obj, ".dynamic");
    if (dyn == NULL) {
        dyn = elf_add_section(obj, ".dynamic", SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE);
        if (dyn == NULL) {
            return obj->last_err == ELF_OK ? ELF_ERR_OOM : obj->last_err;
        }
        dyn->addralign = obj->cls == ELFOBJ_CLASS_64 ? 8 : 4;
    }

    if (obj->cls == ELFOBJ_CLASS_64) {
        entsz = 16;
        elf__wr64(entry + 0, obj->endian, (uint64_t)tag);
        elf__wr64(entry + 8, obj->endian, value);
    } else {
        entsz = 8;
        elf__wr32(entry + 0, obj->endian, (uint32_t)tag);
        elf__wr32(entry + 4, obj->endian, (uint32_t)value);
    }
    err = append_section_data(dyn, entry, entsz, NULL);
    if (err != ELF_OK) {
        return err;
    }
    dyn->entsize = entsz;
    return ELF_OK;
}

elf_err_t elf_link(elfobj_t **inputs, size_t count, elfobj_t **output) {
    elf_link_plan_t *plan;
    elf_err_t err;
    size_t i;
    char namebuf[32];

    if (inputs == NULL || count == 0 || output == NULL) {
        return ELF_ERR_STATE;
    }

    plan = elf_link_plan_create();
    if (plan == NULL) {
        return ELF_ERR_OOM;
    }
    for (i = 0; i < count; ++i) {
        (void)snprintf(namebuf, sizeof(namebuf), "input%lu", (unsigned long)i);
        err = elf_link_plan_add_input(plan, inputs[i], namebuf);
        if (err != ELF_OK) {
            elf_link_plan_destroy(plan);
            return err;
        }
    }
    err = elf_link_plan_link(plan, output);
    elf_link_plan_destroy(plan);
    return err;
}
