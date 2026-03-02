#include "elf_private.h"

static int is_internal_section_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 ||
           strcmp(name, ".shstrtab") == 0 || strncmp(name, ".rel", 4) == 0 ||
           strncmp(name, ".rela", 5) == 0;
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
                                uint64_t *sec_bases, uint8_t *sec_included) {
    size_t j;

    for (j = 0; j < input->obj->section_count; ++j) {
        struct elf_section *src = input->obj->sections[j];
        struct elf_section *dst;
        elf_link_merge_action_t action = ELF_LINK_MERGE_APPEND;
        elf_err_t err;

        if (src == NULL || src->name == NULL || src->name[0] == '\0') {
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
                return out->last_err == ELF_OK ? ELF_ERR_OOM : out->last_err;
            }
            dst->addralign = src->addralign;
        } else if (strcmp(src->name, ".ARM.attributes") == 0 && src->data_size != 0 &&
                   dst->data_size != 0 &&
                   (src->data_size != dst->data_size ||
                    memcmp(src->data, dst->data, src->data_size) != 0)) {
            elf__set_err(out, ELF_ERR_FORMAT, ".ARM.attributes mismatch across inputs");
            return ELF_ERR_FORMAT;
        }

        if (action == ELF_LINK_MERGE_REPLACE) {
            err = replace_section_data(dst, src);
            if (err != ELF_OK) {
                return err;
            }
            sec_bases[j] = 0;
        } else {
            err = append_section_data(dst, src->data, src->data_size, &sec_bases[j]);
            if (err != ELF_OK) {
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

    return ELF_OK;
}

static int resolve_src_sec_index(const elfobj_t *obj, uint16_t shndx, size_t *out_idx) {
    size_t idx;
    struct elf_section *sec;

    if (obj == NULL || out_idx == NULL) {
        return 0;
    }
    if (shndx == SHN_UNDEF || shndx == SHN_ABS || shndx == SHN_COMMON) {
        return 0;
    }
    if (shndx == 0 || shndx > obj->section_count) {
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
                               size_t input_index) {
    size_t i;

    for (i = 0; i < input->obj->symbol_count; ++i) {
        struct elf_symbol *sym = input->obj->symbols[i];
        struct elf_symbol *existing;
        struct elf_symbol *n;
        uint64_t value = 0;
        uint16_t shndx = SHN_UNDEF;
        const char *sec_name = "";

        if (sym == NULL || sym->name == NULL || sym->name[0] == '\0') {
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

        existing = elf_find_symbol(out, sym->name);
        if (existing == NULL) {
            n = elf_add_symbol(out, sym->name, value, sym->size, sym->bind, sym->type);
            if (n == NULL) {
                return out->last_err == ELF_OK ? ELF_ERR_OOM : out->last_err;
            }
            n->other = sym->other;
            n->shndx = shndx;
            n->ver_index = sym->ver_index;
            if (plan_push_map_entry(plan, n->name, sec_name, input->name, n->value, input_index) != ELF_OK) {
                return ELF_ERR_OOM;
            }
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
                                   const uint64_t *sec_bases, const uint8_t *sec_included) {
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

        if (r->symbol == NULL || r->symbol->name == NULL || r->symbol->name[0] == '\0') {
            continue;
        }
        dst_sym = elf_find_symbol(out, r->symbol->name);
        if (dst_sym == NULL) {
            elf__set_err(out, ELF_ERR_RELOC, "unresolved relocation symbol during link");
            (void)elf__append_diag(out, r->symbol->name);
            return ELF_ERR_RELOC;
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
    size_t i;

    if (plan == NULL || output == NULL || plan->input_count == 0) {
        return ELF_ERR_STATE;
    }

    out = elf_create(ET_REL, plan->inputs[0].obj->machine, plan->inputs[0].obj->cls,
                     plan->inputs[0].obj->endian);
    if (out == NULL) {
        return ELF_ERR_OOM;
    }

    free_map_entries(plan);
    out->flags = plan->inputs[0].obj->flags;

    for (i = 0; i < plan->input_count; ++i) {
        const struct elf_link_input *in = &plan->inputs[i];
        uint64_t *sec_bases;
        uint8_t *sec_included;
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
        if ((sec_bases == NULL || sec_included == NULL) && in->obj->section_count != 0) {
            free(sec_bases);
            free(sec_included);
            elf_close(out);
            return ELF_ERR_OOM;
        }

        err = merge_sections(plan, out, in, sec_bases, sec_included);
        if (err == ELF_OK) {
            err = merge_symbols(plan, out, in, sec_bases, sec_included, i);
        }
        if (err == ELF_OK) {
            err = merge_relocations(plan, out, in, sec_bases, sec_included);
        }
        free(sec_bases);
        free(sec_included);
        if (err != ELF_OK) {
            elf_close(out);
            return err;
        }
        (void)elf_link_plan_note_incremental(plan, "merged-input",
                                             in->name != NULL ? in->name : "");
    }

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
