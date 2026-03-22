#include "elf_private.h"

typedef struct {
    const char *name;
    uint32_t sh_name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
    const uint8_t *data;
    uint8_t owns_data;
    uint8_t owns_name;
    size_t source_index;
    uint32_t group_index;
    const char *group_signature;
    uint8_t group_comdat;
} out_sec_t;

typedef struct {
    uint32_t group_index;
    const char *signature;
    uint8_t comdat;
    size_t *members;
    size_t member_count;
    size_t member_cap;
    size_t signature_sym_index;
} out_group_t;

typedef struct {
    const char *name;
    uint16_t index;
} ver_name_t;

#ifndef GRP_COMDAT
#define GRP_COMDAT 0x1u
#endif

static uint64_t align_up(uint64_t v, uint64_t a) {
    if (a <= 1) {
        return v;
    }
    return (v + (a - 1)) & ~(a - 1);
}

static uint64_t align_with_page_mod(uint64_t off, uint64_t align, uint64_t addr) {
    uint64_t want = addr & 0xfffu;
    uint64_t cur;
    uint64_t steps;

    off = align_up(off, align ? align : 1);
    cur = off & 0xfffu;
    if (cur == want) {
        return off;
    }
    if (cur < want) {
        return off + (want - cur);
    }
    steps = 0x1000u - (cur - want);
    return off + steps;
}

static int u64_add_checked(uint64_t a, uint64_t b, uint64_t *out) {
    return elf__u64_add(a, b, out);
}

static int find_alloc_file_bias(const out_sec_t *secs, size_t sec_count, uint64_t *out_bias) {
    size_t i;

    if (secs == NULL || out_bias == NULL) {
        return 0;
    }
    for (i = 1; i < sec_count; ++i) {
        if ((secs[i].flags & SHF_ALLOC) == 0 || secs[i].type == SHT_NOBITS || secs[i].size == 0) {
            continue;
        }
        if (secs[i].addr < secs[i].offset) {
            continue;
        }
        *out_bias = secs[i].addr - secs[i].offset;
        return 1;
    }
    return 0;
}

static uint32_t count_gnu_verdef_entries(const out_sec_t *sec, elfobj_endian_t endian) {
    size_t off = 0;
    uint32_t count = 0;

    if (sec == NULL || sec->data == NULL || sec->size < 20) {
        return 0;
    }
    while (off + 20 <= sec->size) {
        uint32_t next = elf__rd32(sec->data + off + 16, endian);
        count++;
        if (next == 0) {
            break;
        }
        if (next < 20 || next > sec->size - off) {
            break;
        }
        off += next;
    }
    return count;
}

static uint32_t count_gnu_verneed_entries(const out_sec_t *sec, elfobj_endian_t endian) {
    size_t off = 0;
    uint32_t count = 0;

    if (sec == NULL || sec->data == NULL || sec->size < 16) {
        return 0;
    }
    while (off + 16 <= sec->size) {
        uint32_t next = elf__rd32(sec->data + off + 12, endian);
        count++;
        if (next == 0) {
            break;
        }
        if (next < 16 || next > sec->size - off) {
            break;
        }
        off += next;
    }
    return count;
}

static elf_err_t out_push(out_sec_t **secs, size_t *count, size_t *cap, const out_sec_t *in) {
    void *next;
    if (*count == *cap) {
        size_t new_cap = *cap == 0 ? 16 : *cap * 2;
        next = elf__reallocarray(*secs, new_cap, sizeof((*secs)[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        *secs = (out_sec_t *)next;
        *cap = new_cap;
    }
    (*secs)[*count] = *in;
    (*count)++;
    return ELF_OK;
}

static elf_err_t group_member_push(out_group_t *group, size_t member) {
    size_t new_cap;
    void *next;

    if (group->member_count == group->member_cap) {
        new_cap = group->member_cap == 0 ? 8 : group->member_cap * 2;
        next = elf__reallocarray(group->members, new_cap, sizeof(group->members[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        group->members = (size_t *)next;
        group->member_cap = new_cap;
    }
    group->members[group->member_count++] = member;
    return ELF_OK;
}

static ptrdiff_t find_group_desc(const out_group_t *groups, size_t group_count, uint32_t group_index) {
    size_t i;

    for (i = 0; i < group_count; ++i) {
        if (groups[i].group_index == group_index) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static elf_err_t collect_out_groups(const out_sec_t *secs, size_t sec_count, out_group_t **groups_out, size_t *count_out) {
    out_group_t *groups = NULL;
    size_t group_count = 0;
    size_t group_cap = 0;
    size_t i;

    for (i = 1; i < sec_count; ++i) {
        const out_sec_t *sec = &secs[i];
        ptrdiff_t idx;
        void *next;

        if (sec->group_index == 0 || sec->group_signature == NULL || sec->group_signature[0] == '\0') {
            continue;
        }
        idx = find_group_desc(groups, group_count, sec->group_index);
        if (idx < 0) {
            if (group_count == group_cap) {
                group_cap = group_cap == 0 ? 8 : group_cap * 2;
                next = elf__reallocarray(groups, group_cap, sizeof(groups[0]));
                if (next == NULL) {
                    goto oom;
                }
                groups = (out_group_t *)next;
            }
            idx = (ptrdiff_t)group_count++;
            memset(&groups[idx], 0, sizeof(groups[idx]));
            groups[idx].group_index = sec->group_index;
            groups[idx].signature = sec->group_signature;
            groups[idx].comdat = sec->group_comdat;
        }
        if (group_member_push(&groups[idx], i) != ELF_OK) {
            goto oom;
        }
    }

    *groups_out = groups;
    *count_out = group_count;
    return ELF_OK;

oom:
    if (groups != NULL) {
        for (i = 0; i < group_count; ++i) {
            free(groups[i].members);
        }
        free(groups);
    }
    return ELF_ERR_OOM;
}

static void free_out_groups(out_group_t *groups, size_t group_count) {
    size_t i;

    if (groups == NULL) {
        return;
    }
    for (i = 0; i < group_count; ++i) {
        free(groups[i].members);
    }
    free(groups);
}

static uint8_t *build_group_data(const out_group_t *group, elfobj_endian_t endian, size_t *out_size) {
    uint8_t *data;
    size_t i;
    size_t words;

    if (group == NULL || out_size == NULL) {
        return NULL;
    }
    words = 1 + group->member_count;
    data = (uint8_t *)elf__calloc(words, 4);
    if (data == NULL) {
        return NULL;
    }
    elf__wr32(data + 0, endian, group->comdat ? GRP_COMDAT : 0);
    for (i = 0; i < group->member_count; ++i) {
        elf__wr32(data + ((i + 1) * 4), endian, (uint32_t)group->members[i]);
    }
    *out_size = words * 4;
    return data;
}

static uint8_t *build_symtab(const elfobj_t *obj, elfobj_endian_t e, elfobj_class_t cls,
                             elf_strtab_t *strtab, const char **extra_names, size_t extra_count,
                             size_t *extra_indices, size_t *out_size, size_t *out_entsize) {
    size_t entsz = (cls == ELFOBJ_CLASS_64) ? 24 : 16;
    int has_null = (obj->symbol_count > 0 &&
                    (obj->symbols[0]->name == NULL || obj->symbols[0]->name[0] == '\0') &&
                    obj->symbols[0]->value == 0 &&
                    obj->symbols[0]->size == 0);
    size_t n = obj->symbol_count + (has_null ? 0 : 1) + extra_count;
    uint8_t *buf = (uint8_t *)elf__calloc(n, entsz);
    size_t i;

    if (buf == NULL) {
        return NULL;
    }

    for (i = 0; i < obj->symbol_count; ++i) {
        const struct elf_symbol *sym = obj->symbols[i];
        uint8_t *p = buf + ((has_null ? i : (i + 1)) * entsz);
        uint32_t st_name = 0;
        uint8_t st_info = ELF32_ST_INFO(sym->bind, sym->type);
        if (!(has_null && i == 0)) {
            st_name = elf__strtab_add(strtab, sym->name ? sym->name : "");
        }
        if (cls == ELFOBJ_CLASS_32) {
            elf__wr32(p + 0, e, st_name);
            elf__wr32(p + 4, e, (uint32_t)sym->value);
            elf__wr32(p + 8, e, (uint32_t)sym->size);
            p[12] = st_info;
            p[13] = sym->other;
            elf__wr16(p + 14, e, sym->shndx);
        } else {
            elf__wr32(p + 0, e, st_name);
            p[4] = st_info;
            p[5] = sym->other;
            elf__wr16(p + 6, e, sym->shndx);
            elf__wr64(p + 8, e, sym->value);
            elf__wr64(p + 16, e, sym->size);
        }
    }

    for (i = 0; i < extra_count; ++i) {
        size_t sym_index = obj->symbol_count + (has_null ? 0 : 1) + i;
        uint8_t *p = buf + (sym_index * entsz);
        uint32_t st_name = elf__strtab_add(strtab, extra_names[i] ? extra_names[i] : "");
        uint8_t st_info = ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE);

        if (extra_indices != NULL) {
            extra_indices[i] = sym_index;
        }
        if (cls == ELFOBJ_CLASS_32) {
            elf__wr32(p + 0, e, st_name);
            elf__wr32(p + 4, e, 0);
            elf__wr32(p + 8, e, 0);
            p[12] = st_info;
            p[13] = 0;
            elf__wr16(p + 14, e, SHN_UNDEF);
        } else {
            elf__wr32(p + 0, e, st_name);
            p[4] = st_info;
            p[5] = 0;
            elf__wr16(p + 6, e, SHN_UNDEF);
            elf__wr64(p + 8, e, 0);
            elf__wr64(p + 16, e, 0);
        }
    }

    *out_size = n * entsz;
    *out_entsize = entsz;
    return buf;
}

static uint16_t ver_name_lookup(const ver_name_t *names, size_t count, const char *name) {
    size_t i;
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (names[i].name != NULL && strcmp(names[i].name, name) == 0) {
            return names[i].index;
        }
    }
    return 0;
}

static elf_err_t collect_version_names(const elfobj_t *obj, ver_name_t **out_names, size_t *out_count) {
    ver_name_t *names = NULL;
    size_t count = 0;
    size_t cap = 0;
    size_t i;

    if (out_names == NULL || out_count == NULL) {
        return ELF_ERR_STATE;
    }
    *out_names = NULL;
    *out_count = 0;

    for (i = 0; i < obj->symbol_count; ++i) {
        const struct elf_symbol *sym = obj->symbols[i];
        uint16_t idx;
        if (sym == NULL || sym->version_name == NULL || sym->version_name[0] == '\0') {
            continue;
        }
        idx = ver_name_lookup(names, count, sym->version_name);
        if (idx != 0) {
            continue;
        }
        if (count == cap) {
            size_t new_cap = cap == 0 ? 8 : cap * 2;
            void *next = elf__reallocarray(names, new_cap, sizeof(names[0]));
            if (next == NULL) {
                free(names);
                return ELF_ERR_OOM;
            }
            names = (ver_name_t *)next;
            cap = new_cap;
        }
        names[count].name = sym->version_name;
        names[count].index = (uint16_t)(count + 2);
        count++;
    }

    *out_names = names;
    *out_count = count;
    return ELF_OK;
}

static uint8_t *build_gnu_versym(const elfobj_t *obj, elfobj_endian_t e, const ver_name_t *names,
                                 size_t name_count, size_t extra_count, size_t *out_size) {
    int has_null = (obj->symbol_count > 0 &&
                    (obj->symbols[0]->name == NULL || obj->symbols[0]->name[0] == '\0') &&
                    obj->symbols[0]->value == 0 &&
                    obj->symbols[0]->size == 0);
    size_t nsyms = obj->symbol_count + (has_null ? 0 : 1) + extra_count;
    uint8_t *buf;
    size_t i;

    if (out_size == NULL) {
        return NULL;
    }
    *out_size = 0;
    if (nsyms == 0) {
        return NULL;
    }
    buf = (uint8_t *)elf__calloc(nsyms, 2);
    if (buf == NULL) {
        return NULL;
    }

    for (i = 0; i < nsyms; ++i) {
        elf__wr16(buf + (i * 2), e, VER_NDX_GLOBAL);
    }
    elf__wr16(buf + 0, e, VER_NDX_LOCAL);

    for (i = 0; i < obj->symbol_count; ++i) {
        const struct elf_symbol *sym = obj->symbols[i];
        size_t idx = has_null ? i : (i + 1);
        uint16_t ver = VER_NDX_GLOBAL;

        if (sym == NULL) {
            continue;
        }
        if (sym->bind == STB_LOCAL) {
            ver = VER_NDX_LOCAL;
        }
        if (sym->version_name != NULL && sym->version_name[0] != '\0') {
            uint16_t v = ver_name_lookup(names, name_count, sym->version_name);
            if (v != 0) {
                ver = v;
                if (!sym->version_default) {
                    ver = (uint16_t)(ver | VER_NDX_HIDDEN);
                }
            }
        }
        if (idx < nsyms) {
            elf__wr16(buf + (idx * 2), e, ver);
        }
    }

    *out_size = nsyms * 2;
    return buf;
}

static uint8_t *build_gnu_verdef(elfobj_endian_t e, elf_strtab_t *strtab,
                                 const ver_name_t *names, size_t name_count, size_t *out_size) {
    size_t verdef_sz = 20;
    size_t verdaux_sz = 8;
    size_t total = name_count * (verdef_sz + verdaux_sz);
    uint8_t *buf;
    size_t i;

    if (out_size == NULL) {
        return NULL;
    }
    *out_size = 0;
    if (name_count == 0) {
        return NULL;
    }
    buf = (uint8_t *)elf__calloc(1, total);
    if (buf == NULL) {
        return NULL;
    }
    for (i = 0; i < name_count; ++i) {
        uint8_t *p = buf + (i * (verdef_sz + verdaux_sz));
        uint32_t name_off = elf__strtab_add(strtab, names[i].name ? names[i].name : "");
        uint32_t hash = elf_hash_sysv(names[i].name ? names[i].name : "");

        elf__wr16(p + 0, e, 1);
        elf__wr16(p + 2, e, 0);
        elf__wr16(p + 4, e, names[i].index);
        elf__wr16(p + 6, e, 1);
        elf__wr32(p + 8, e, hash);
        elf__wr32(p + 12, e, (uint32_t)verdef_sz);
        elf__wr32(p + 16, e, (uint32_t)((i + 1 < name_count) ? (verdef_sz + verdaux_sz) : 0));

        elf__wr32(p + verdef_sz + 0, e, name_off);
        elf__wr32(p + verdef_sz + 4, e, 0);
    }

    *out_size = total;
    return buf;
}

static uint8_t *build_relocs_for_section(const elfobj_t *obj, const struct elf_section *target,
                                         int with_addend,
                                         size_t *out_size, size_t *out_entsize) {
    size_t i;
    size_t n = 0;
    size_t entsz = 0;
    uint8_t *buf;
    int use_rela_default = (obj->cls == ELFOBJ_CLASS_64);

    if (obj->machine == EM_ARM) {
        use_rela_default = 0;
    } else if (obj->machine == EM_AARCH64) {
        use_rela_default = 1;
    } else if (obj->machine == EM_RISCV) {
        use_rela_default = 1;
    } else if (obj->machine == EM_LOONGARCH) {
        use_rela_default = 1;
    } else if (obj->machine == EM_68K) {
        use_rela_default = 1;
    } else if (obj->machine == EM_VAX) {
        use_rela_default = 1;
    } else if (obj->machine == EM_PPC || obj->machine == EM_PPC64) {
        use_rela_default = 1;
    } else if (obj->machine == EM_ALPHA) {
        use_rela_default = 1;
    } else if (obj->machine == EM_IA_64) {
        use_rela_default = 1;
    }

    for (i = 0; i < target->reloc_count; ++i) {
        const struct elf_reloc *r = target->relocs[i];
        int reloc_has_addend;
        if (r == NULL) {
            continue;
        }
        reloc_has_addend = use_rela_default;
        if (obj->machine != EM_ARM && obj->machine != EM_AARCH64 &&
            obj->machine != EM_RISCV && obj->machine != EM_LOONGARCH &&
            obj->machine != EM_68K && obj->machine != EM_VAX &&
            obj->machine != EM_PPC && obj->machine != EM_PPC64 &&
            obj->machine != EM_ALPHA && obj->machine != EM_IA_64) {
            reloc_has_addend = r->has_addend != 0;
        }
        if ((reloc_has_addend != 0) == (with_addend != 0)) {
            n++;
        }
    }
    if (obj->cls == ELFOBJ_CLASS_64) {
        entsz = with_addend ? 24 : 16;
    } else {
        entsz = with_addend ? 12 : 8;
    }

    if (n == 0) {
        *out_size = 0;
        *out_entsize = entsz;
        return NULL;
    }

    buf = (uint8_t *)elf__calloc(n, entsz);
    if (buf == NULL) {
        return NULL;
    }

    n = 0;
    for (i = 0; i < target->reloc_count; ++i) {
        const struct elf_reloc *r = target->relocs[i];
        size_t sym_index = 0;
        uint8_t *p;
        int reloc_has_addend;

        if (r == NULL) {
            continue;
        }
        reloc_has_addend = use_rela_default;
        if (obj->machine != EM_ARM && obj->machine != EM_AARCH64 &&
            obj->machine != EM_RISCV && obj->machine != EM_LOONGARCH &&
            obj->machine != EM_68K && obj->machine != EM_VAX &&
            obj->machine != EM_PPC && obj->machine != EM_PPC64 &&
            obj->machine != EM_ALPHA && obj->machine != EM_IA_64) {
            reloc_has_addend = r->has_addend != 0;
        }
        if ((reloc_has_addend != 0) != (with_addend != 0)) {
            continue;
        }
        p = buf + (n * entsz);
        n++;

        if (r->symbol != NULL) {
            sym_index = r->symbol->index + 1;
        }

        if (obj->cls == ELFOBJ_CLASS_32) {
            uint32_t info = ELF32_R_INFO((uint32_t)sym_index, r->type);
            elf__wr32(p + 0, obj->endian, (uint32_t)r->offset);
            elf__wr32(p + 4, obj->endian, info);
            if (with_addend) {
                elf__wr32(p + 8, obj->endian, (uint32_t)r->addend);
            }
        } else {
            uint64_t info = ELF64_R_INFO((uint64_t)sym_index, r->type);
            elf__wr64(p + 0, obj->endian, r->offset);
            elf__wr64(p + 8, obj->endian, info);
            if (with_addend) {
                elf__wr64(p + 16, obj->endian, (uint64_t)r->addend);
            }
        }
    }

    *out_size = n * entsz;
    *out_entsize = entsz;
    return buf;
}

static uint8_t *build_dynstr(const elfobj_t *obj, size_t *out_size) {
    elf_strtab_t tab;
    size_t i;

    if (elf__strtab_init(&tab) != ELF_OK) {
        return NULL;
    }
    for (i = 0; i < obj->symbol_count; ++i) {
        const struct elf_symbol *sym = obj->symbols[i];
        if (sym == NULL || sym->name == NULL) {
            continue;
        }
        if (sym->bind == STB_GLOBAL || sym->bind == STB_WEAK) {
            (void)elf__strtab_add(&tab, sym->name);
        }
    }
    *out_size = tab.size;
    return (uint8_t *)tab.data;
}

elf_err_t elf__write_to_buffer(elfobj_t *obj, uint8_t **out_buf, size_t *out_sz) {
    out_sec_t *secs = NULL;
    out_group_t *groups = NULL;
    size_t sec_count = 0;
    size_t sec_cap = 0;
    size_t group_count = 0;
    elf_strtab_t shstr;
    elf_strtab_t strtab;
    size_t symtab_index = 0;
    size_t strtab_index = 0;
    size_t shstr_index = 0;
    size_t i;
    size_t *obj_sec_to_out = NULL;
    const char **group_sig_names = NULL;
    size_t *group_sig_indices = NULL;
    uint8_t *symtab_data = NULL;
    uint8_t *dynstr_data = NULL;
    ver_name_t *ver_names = NULL;
    size_t ver_name_count = 0;
    uint8_t *versym_data = NULL;
    uint8_t *verdef_data = NULL;
    size_t versym_size = 0;
    size_t verdef_size = 0;
    size_t dynstr_size = 0;
    int has_dynstr_section = 0;
    int has_gnu_versym = 0;
    int has_gnu_verdef = 0;
    size_t symtab_size = 0;
    size_t symtab_entsz = 0;
    uint64_t shoff;
    uint64_t off;
    uint64_t phoff = 0;
    uint8_t *img = NULL;
    size_t ehsize;
    size_t phentsz;
    size_t phnum = 0;
    size_t shentsz;
    elf_err_t err;

    if (obj == NULL || out_buf == NULL || out_sz == NULL) {
        return ELF_ERR_STATE;
    }
    ehsize = obj->cls == ELFOBJ_CLASS_64 ? sizeof(Elf64_Ehdr) : sizeof(Elf32_Ehdr);
    phentsz = obj->cls == ELFOBJ_CLASS_64 ? sizeof(Elf64_Phdr) : sizeof(Elf32_Phdr);
    shentsz = obj->cls == ELFOBJ_CLASS_64 ? 64 : 40;

    err = elf__strtab_init(&shstr);
    if (err != ELF_OK) {
        return err;
    }
    err = elf__strtab_init(&strtab);
    if (err != ELF_OK) {
        elf__strtab_free(&shstr);
        return err;
    }

    {
        out_sec_t nulls;
        memset(&nulls, 0, sizeof(nulls));
        out_push(&secs, &sec_count, &sec_cap, &nulls);
    }

    obj_sec_to_out = (size_t *)elf__calloc(obj->section_count, sizeof(*obj_sec_to_out));
    if (obj_sec_to_out == NULL) {
        err = ELF_ERR_OOM;
        goto done;
    }

    for (i = 0; i < obj->section_count; ++i) {
        const struct elf_section *s = obj->sections[i];
        const char *name = s->name ? s->name : "";
        out_sec_t out;

        if (s->type == SHT_NULL ||
            strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 ||
            strcmp(name, ".shstrtab") == 0 ||
            ((s->type == SHT_REL || s->type == SHT_RELA) &&
             (strncmp(name, ".rel", 4) == 0 || strncmp(name, ".rela", 5) == 0) &&
             (s->flags & SHF_ALLOC) == 0)) {
            continue;
        }
        memset(&out, 0, sizeof(out));
        out.name = name;
        out.type = s->type;
        out.flags = s->flags;
        out.addr = s->addr;
        out.link = s->link;
        out.info = s->info;
        out.addralign = s->addralign ? s->addralign : 1;
        out.entsize = s->entsize;
        out.data = s->data;
        out.size = s->type == SHT_NOBITS ? s->size : s->data_size;
        out.source_index = i;
        out.group_index = s->group_index;
        out.group_signature = s->group_signature;
        out.group_comdat = s->comdat;
        if (strcmp(name, ".dynstr") == 0) {
            has_dynstr_section = 1;
        } else if (strcmp(name, ".gnu.version") == 0) {
            has_gnu_versym = 1;
        } else if (strcmp(name, ".gnu.version_d") == 0) {
            has_gnu_verdef = 1;
        }
        obj_sec_to_out[i] = sec_count;
        if (out_push(&secs, &sec_count, &sec_cap, &out) != ELF_OK) {
            err = ELF_ERR_OOM;
            goto done;
        }
    }

    for (i = 0; i < obj->section_count; ++i) {
        const struct elf_section *target = obj->sections[i];
        uint8_t *rel_data;
        size_t rel_size = 0;
        size_t rel_ent = 0;
        out_sec_t out;
        char *name;
        size_t nlen;
        int with_addend;

        if (target->reloc_count == 0) {
            continue;
        }

        for (with_addend = 0; with_addend <= 1; ++with_addend) {
            rel_data = build_relocs_for_section(obj, target, with_addend, &rel_size, &rel_ent);
            if (rel_data == NULL && rel_size != 0) {
                err = ELF_ERR_OOM;
                goto done;
            }
            if (rel_size == 0) {
                free(rel_data);
                continue;
            }

            nlen = strlen(target->name ? target->name : "") + (with_addend ? 6 : 5);
            name = (char *)malloc(nlen);
            if (name == NULL) {
                free(rel_data);
                err = ELF_ERR_OOM;
                goto done;
            }
            if (with_addend) {
                memcpy(name, ".rela", 5);
                memcpy(name + 5, target->name ? target->name : "", nlen - 5);
            } else {
                memcpy(name, ".rel", 4);
                memcpy(name + 4, target->name ? target->name : "", nlen - 4);
            }

            memset(&out, 0, sizeof(out));
            out.name = name;
            out.type = with_addend ? SHT_RELA : SHT_REL;
            out.flags = 0;
            out.addralign = obj->cls == ELFOBJ_CLASS_64 ? 8 : 4;
            out.entsize = rel_ent;
            out.data = rel_data;
            out.size = rel_size;
            out.owns_data = 1;
            out.owns_name = 1;
            out.info = obj_sec_to_out[i] != 0 ? (uint32_t)obj_sec_to_out[i] : 0;
            out.group_index = target->group_index;
            out.group_signature = target->group_signature;
            out.group_comdat = target->comdat;
            if (out_push(&secs, &sec_count, &sec_cap, &out) != ELF_OK) {
                free(name);
                free(rel_data);
                err = ELF_ERR_OOM;
                goto done;
            }
        }
    }

    err = collect_out_groups(secs, sec_count, &groups, &group_count);
    if (err != ELF_OK) {
        goto done;
    }
    if (group_count != 0) {
        group_sig_names = (const char **)elf__calloc(group_count, sizeof(*group_sig_names));
        group_sig_indices = (size_t *)elf__calloc(group_count, sizeof(*group_sig_indices));
        if (group_sig_names == NULL || group_sig_indices == NULL) {
            err = ELF_ERR_OOM;
            goto done;
        }
        for (i = 0; i < group_count; ++i) {
            group_sig_names[i] = groups[i].signature;
        }
    }

    symtab_data = build_symtab(obj, obj->endian, obj->cls, &strtab, group_sig_names, group_count,
                               group_sig_indices, &symtab_size, &symtab_entsz);
    if (symtab_data == NULL) {
        err = ELF_ERR_OOM;
        goto done;
    }

    for (i = 0; i < group_count; ++i) {
        out_sec_t grpsec;
        size_t grp_size = 0;
        uint8_t *grp_data = build_group_data(&groups[i], obj->endian, &grp_size);

        if (grp_data == NULL) {
            err = ELF_ERR_OOM;
            goto done;
        }
        groups[i].signature_sym_index = group_sig_indices[i];
        memset(&grpsec, 0, sizeof(grpsec));
        grpsec.name = ".group";
        grpsec.type = SHT_GROUP;
        grpsec.flags = 0;
        grpsec.link = 0;
        grpsec.info = (uint32_t)groups[i].signature_sym_index;
        grpsec.addralign = 4;
        grpsec.entsize = 4;
        grpsec.data = grp_data;
        grpsec.size = grp_size;
        grpsec.owns_data = 1;
        if (out_push(&secs, &sec_count, &sec_cap, &grpsec) != ELF_OK) {
            free(grp_data);
            err = ELF_ERR_OOM;
            goto done;
        }
    }

    {
        out_sec_t symsec;
        memset(&symsec, 0, sizeof(symsec));
        symsec.name = ".symtab";
        symsec.type = SHT_SYMTAB;
        symsec.flags = 0;
        symsec.addralign = obj->cls == ELFOBJ_CLASS_64 ? 8 : 4;
        symsec.entsize = symtab_entsz;
        symsec.data = symtab_data;
        symsec.size = symtab_size;
        symsec.owns_data = 1;
        symtab_index = sec_count;
        if (out_push(&secs, &sec_count, &sec_cap, &symsec) != ELF_OK) {
            err = ELF_ERR_OOM;
            goto done;
        }
    }

    if (obj->has_versioning && !has_gnu_versym && !has_gnu_verdef) {
        err = collect_version_names(obj, &ver_names, &ver_name_count);
        if (err != ELF_OK) {
            goto done;
        }
        if (ver_name_count != 0) {
            verdef_data = build_gnu_verdef(obj->endian, &strtab, ver_names, ver_name_count, &verdef_size);
            if (verdef_data == NULL || verdef_size == 0) {
                err = ELF_ERR_OOM;
                goto done;
            }
            versym_data = build_gnu_versym(obj, obj->endian, ver_names, ver_name_count, group_count, &versym_size);
            if (versym_data == NULL || versym_size == 0) {
                err = ELF_ERR_OOM;
                goto done;
            }
        }
    }

    {
        out_sec_t stsec;
        memset(&stsec, 0, sizeof(stsec));
        stsec.name = ".strtab";
        stsec.type = SHT_STRTAB;
        stsec.flags = 0;
        stsec.addralign = 1;
        stsec.data = (const uint8_t *)strtab.data;
        stsec.size = strtab.size;
        strtab_index = sec_count;
        if (out_push(&secs, &sec_count, &sec_cap, &stsec) != ELF_OK) {
            err = ELF_ERR_OOM;
            goto done;
        }
    }

    if (verdef_data != NULL && verdef_size != 0) {
        out_sec_t vsec;
        memset(&vsec, 0, sizeof(vsec));
        vsec.name = ".gnu.version_d";
        vsec.type = SHT_GNU_verdef;
        vsec.flags = 0;
        vsec.addralign = 4;
        vsec.entsize = 20;
        vsec.data = verdef_data;
        vsec.size = verdef_size;
        vsec.owns_data = 1;
        vsec.link = (uint32_t)strtab_index;
        vsec.info = (uint32_t)ver_name_count;
        if (out_push(&secs, &sec_count, &sec_cap, &vsec) != ELF_OK) {
            err = ELF_ERR_OOM;
            goto done;
        }
        verdef_data = NULL;
    }

    if (versym_data != NULL && versym_size != 0) {
        out_sec_t vsec;
        memset(&vsec, 0, sizeof(vsec));
        vsec.name = ".gnu.version";
        vsec.type = SHT_GNU_versym;
        vsec.flags = 0;
        vsec.addralign = 2;
        vsec.entsize = 2;
        vsec.data = versym_data;
        vsec.size = versym_size;
        vsec.owns_data = 1;
        vsec.link = (uint32_t)symtab_index;
        if (out_push(&secs, &sec_count, &sec_cap, &vsec) != ELF_OK) {
            err = ELF_ERR_OOM;
            goto done;
        }
        versym_data = NULL;
    }

    if ((obj->type == ET_EXEC || obj->type == ET_DYN) && !has_dynstr_section) {
        out_sec_t dynst;
        dynstr_data = build_dynstr(obj, &dynstr_size);
        if (dynstr_data == NULL && dynstr_size != 0) {
            err = ELF_ERR_OOM;
            goto done;
        }
        memset(&dynst, 0, sizeof(dynst));
        dynst.name = ".dynstr";
        dynst.type = SHT_STRTAB;
        dynst.flags = SHF_ALLOC;
        dynst.addralign = 1;
        dynst.data = dynstr_data;
        dynst.size = dynstr_size;
        dynst.owns_data = 1;
        if (out_push(&secs, &sec_count, &sec_cap, &dynst) != ELF_OK) {
            err = ELF_ERR_OOM;
            goto done;
        }
    }

    {
        out_sec_t shsec;
        memset(&shsec, 0, sizeof(shsec));
        shsec.name = ".shstrtab";
        shsec.type = SHT_STRTAB;
        shsec.flags = 0;
        shsec.addralign = 1;
        shstr_index = sec_count;
        if (out_push(&secs, &sec_count, &sec_cap, &shsec) != ELF_OK) {
            err = ELF_ERR_OOM;
            goto done;
        }
    }

    for (i = 1; i < sec_count; ++i) {
        secs[i].sh_name = elf__strtab_add(&shstr, secs[i].name ? secs[i].name : "");
    }
    secs[shstr_index].data = (const uint8_t *)shstr.data;
    secs[shstr_index].size = shstr.size;

    secs[symtab_index].link = (uint32_t)strtab_index;
    {
        int has_null = (obj->symbol_count > 0 &&
                        (obj->symbols[0]->name == NULL || obj->symbols[0]->name[0] == '\0') &&
                        obj->symbols[0]->value == 0 &&
                        obj->symbols[0]->size == 0);
        size_t first_global = obj->symbol_count + (has_null ? 0 : 1);
        for (i = 0; i < obj->symbol_count; ++i) {
            if (obj->symbols[i]->bind != STB_LOCAL) {
                first_global = i + (has_null ? 0 : 1);
                break;
            }
        }
        secs[symtab_index].info = (uint32_t)first_global;
    }

    for (i = 1; i < sec_count; ++i) {
        if (secs[i].type == SHT_RELA || secs[i].type == SHT_REL) {
            secs[i].link = (uint32_t)symtab_index;
        } else if (secs[i].type == SHT_GROUP) {
            secs[i].link = (uint32_t)symtab_index;
        }
    }
    {
        size_t dynsym_index = (size_t)-1;
        size_t dynstr_index = (size_t)-1;
        size_t dynamic_index = (size_t)-1;
        size_t hash_index = (size_t)-1;
        size_t gnu_hash_index = (size_t)-1;
        size_t gnu_versym_index = (size_t)-1;
        size_t gnu_verdef_index = (size_t)-1;
        size_t gnu_verneed_index = (size_t)-1;
        size_t rela_plt_index = (size_t)-1;
        size_t rel_plt_index = (size_t)-1;
        size_t rela_dyn_index = (size_t)-1;
        size_t rel_dyn_index = (size_t)-1;
        size_t plt_index = (size_t)-1;
        size_t gotplt_index = (size_t)-1;
        size_t got_index = (size_t)-1;
        for (i = 1; i < sec_count; ++i) {
            const char *nm = secs[i].name != NULL ? secs[i].name : "";
            if (strcmp(nm, ".dynsym") == 0) {
                dynsym_index = i;
            } else if (strcmp(nm, ".dynstr") == 0) {
                dynstr_index = i;
            } else if (strcmp(nm, ".dynamic") == 0) {
                dynamic_index = i;
            } else if (strcmp(nm, ".hash") == 0) {
                hash_index = i;
            } else if (strcmp(nm, ".gnu.hash") == 0) {
                gnu_hash_index = i;
            } else if (strcmp(nm, ".gnu.version") == 0) {
                gnu_versym_index = i;
            } else if (strcmp(nm, ".gnu.version_d") == 0) {
                gnu_verdef_index = i;
            } else if (strcmp(nm, ".gnu.version_r") == 0) {
                gnu_verneed_index = i;
            } else if (strcmp(nm, ".rela.plt") == 0) {
                rela_plt_index = i;
            } else if (strcmp(nm, ".rel.plt") == 0) {
                rel_plt_index = i;
            } else if (strcmp(nm, ".rela.dyn") == 0) {
                rela_dyn_index = i;
            } else if (strcmp(nm, ".rel.dyn") == 0) {
                rel_dyn_index = i;
            } else if (strcmp(nm, ".plt") == 0) {
                plt_index = i;
            } else if (strcmp(nm, ".got.plt") == 0) {
                gotplt_index = i;
            } else if (strcmp(nm, ".got") == 0) {
                got_index = i;
            }
        }
        if (dynsym_index != (size_t)-1) {
            if (secs[dynsym_index].entsize == 0) {
                secs[dynsym_index].entsize = obj->cls == ELFOBJ_CLASS_64 ? 24 : 16;
            }
            if (secs[dynsym_index].info == 0) {
                secs[dynsym_index].info = 1;
            }
            if (dynstr_index != (size_t)-1) {
                secs[dynsym_index].link = (uint32_t)dynstr_index;
            }
        }
        if (dynamic_index != (size_t)-1) {
            if (secs[dynamic_index].entsize == 0) {
                secs[dynamic_index].entsize = obj->cls == ELFOBJ_CLASS_64 ? 16 : 8;
            }
            if (dynstr_index != (size_t)-1) {
                secs[dynamic_index].link = (uint32_t)dynstr_index;
            }
        }
        if (dynsym_index != (size_t)-1) {
            if (hash_index != (size_t)-1) {
                if (secs[hash_index].entsize == 0) {
                    secs[hash_index].entsize = 4;
                }
                secs[hash_index].link = (uint32_t)dynsym_index;
            }
            if (gnu_hash_index != (size_t)-1) {
                secs[gnu_hash_index].link = (uint32_t)dynsym_index;
            }
            if (gnu_versym_index != (size_t)-1) {
                if (secs[gnu_versym_index].entsize == 0) {
                    secs[gnu_versym_index].entsize = 2;
                }
                secs[gnu_versym_index].link = (uint32_t)dynsym_index;
            }
            if (gnu_verdef_index != (size_t)-1) {
                if (secs[gnu_verdef_index].entsize == 0) {
                    secs[gnu_verdef_index].entsize = 20;
                }
                if (dynstr_index != (size_t)-1) {
                    secs[gnu_verdef_index].link = (uint32_t)dynstr_index;
                }
                if (secs[gnu_verdef_index].info == 0) {
                    secs[gnu_verdef_index].info =
                        count_gnu_verdef_entries(&secs[gnu_verdef_index], obj->endian);
                }
            }
            if (gnu_verneed_index != (size_t)-1) {
                if (secs[gnu_verneed_index].entsize == 0) {
                    secs[gnu_verneed_index].entsize = 16;
                }
                if (dynstr_index != (size_t)-1) {
                    secs[gnu_verneed_index].link = (uint32_t)dynstr_index;
                }
                if (secs[gnu_verneed_index].info == 0) {
                    secs[gnu_verneed_index].info =
                        count_gnu_verneed_entries(&secs[gnu_verneed_index], obj->endian);
                }
            }
            if (rela_plt_index != (size_t)-1) {
                if (secs[rela_plt_index].entsize == 0) {
                    secs[rela_plt_index].entsize = obj->cls == ELFOBJ_CLASS_64 ? 24 : 12;
                }
                secs[rela_plt_index].link = (uint32_t)dynsym_index;
                if (plt_index != (size_t)-1 && secs[rela_plt_index].info == 0) {
                    secs[rela_plt_index].info = (uint32_t)plt_index;
                } else if (gotplt_index != (size_t)-1 && secs[rela_plt_index].info == 0) {
                    secs[rela_plt_index].info = (uint32_t)gotplt_index;
                }
            }
            if (rel_plt_index != (size_t)-1) {
                if (secs[rel_plt_index].entsize == 0) {
                    secs[rel_plt_index].entsize = obj->cls == ELFOBJ_CLASS_64 ? 16 : 8;
                }
                secs[rel_plt_index].link = (uint32_t)dynsym_index;
                if (plt_index != (size_t)-1 && secs[rel_plt_index].info == 0) {
                    secs[rel_plt_index].info = (uint32_t)plt_index;
                } else if (gotplt_index != (size_t)-1 && secs[rel_plt_index].info == 0) {
                    secs[rel_plt_index].info = (uint32_t)gotplt_index;
                }
            }
            if (rela_dyn_index != (size_t)-1) {
                if (secs[rela_dyn_index].entsize == 0) {
                    secs[rela_dyn_index].entsize = obj->cls == ELFOBJ_CLASS_64 ? 24 : 12;
                }
                secs[rela_dyn_index].link = (uint32_t)dynsym_index;
                if (got_index != (size_t)-1 && secs[rela_dyn_index].info == 0) {
                    secs[rela_dyn_index].info = (uint32_t)got_index;
                }
            }
            if (rel_dyn_index != (size_t)-1) {
                if (secs[rel_dyn_index].entsize == 0) {
                    secs[rel_dyn_index].entsize = obj->cls == ELFOBJ_CLASS_64 ? 16 : 8;
                }
                secs[rel_dyn_index].link = (uint32_t)dynsym_index;
                if (got_index != (size_t)-1 && secs[rel_dyn_index].info == 0) {
                    secs[rel_dyn_index].info = (uint32_t)got_index;
                }
            }
        }
    }

    if (obj->segment_count != 0) {
        phnum = obj->segment_count;
    } else if (obj->phdr_count != 0) {
        phnum = obj->phdr_count;
    } else if (obj->type == ET_EXEC || obj->type == ET_DYN) {
        int has_dynamic = 0;
        int has_tls = 0;
        int has_interp = 0;
        phnum = 1;
        for (i = 1; i < sec_count; ++i) {
            if (secs[i].type == SHT_DYNAMIC) {
                has_dynamic = 1;
            }
            if ((secs[i].flags & SHF_TLS) != 0) {
                has_tls = 1;
            }
            if (strcmp(secs[i].name ? secs[i].name : "", ".interp") == 0) {
                has_interp = 1;
            }
        }
        if (has_dynamic) {
            phnum++;
        }
        if (has_interp) {
            phnum++;
        }
        if (has_tls) {
            phnum++;
        }
    }
    phoff = phnum ? (uint64_t)ehsize : 0;
    off = (uint64_t)ehsize;
    if (phnum != 0) {
        uint64_t phbytes;
        if (!elf__u64_mul((uint64_t)phnum, (uint64_t)phentsz, &phbytes)) {
            err = ELF_ERR_BOUNDS;
            goto done;
        }
        if (!u64_add_checked(off, phbytes, &off)) {
            err = ELF_ERR_BOUNDS;
            goto done;
        }
    }
    for (i = 1; i < sec_count; ++i) {
        if (secs[i].type == SHT_NOBITS) {
            secs[i].offset = 0;
            continue;
        }
        if ((secs[i].flags & SHF_ALLOC) != 0 && secs[i].addr != 0) {
            off = align_with_page_mod(off, secs[i].addralign ? secs[i].addralign : 1, secs[i].addr);
        } else {
            off = align_up(off, secs[i].addralign ? secs[i].addralign : 1);
        }
        secs[i].offset = off;
        if (!u64_add_checked(off, secs[i].size, &off)) {
            err = ELF_ERR_BOUNDS;
            goto done;
        }
    }

    shoff = align_up(off, obj->cls == ELFOBJ_CLASS_64 ? 8 : 4);
    {
        uint64_t sht_bytes;
        uint64_t total_sz;
        if (!elf__u64_mul((uint64_t)sec_count, (uint64_t)shentsz, &sht_bytes)) {
            err = ELF_ERR_BOUNDS;
            goto done;
        }
        if (!u64_add_checked(shoff, sht_bytes, &total_sz) || total_sz > SIZE_MAX) {
            err = ELF_ERR_BOUNDS;
            goto done;
        }
        *out_sz = (size_t)total_sz;
    }
    img = (uint8_t *)calloc(1, *out_sz);
    if (img == NULL) {
        err = ELF_ERR_OOM;
        goto done;
    }

    if (obj->cls == ELFOBJ_CLASS_32) {
        img[0] = ELFMAG0;
        img[1] = ELFMAG1;
        img[2] = ELFMAG2;
        img[3] = ELFMAG3;
        img[EI_CLASS] = ELFCLASS32;
        img[EI_DATA] = obj->endian == ELFOBJ_ENDIAN_BE ? ELFDATA2MSB : ELFDATA2LSB;
        img[EI_VERSION] = EV_CURRENT;
        img[EI_OSABI] = obj->osabi;
        img[EI_ABIVERSION] = obj->abiversion;
        elf__wr16(img + 16, obj->endian, obj->type);
        elf__wr16(img + 18, obj->endian, obj->machine);
        elf__wr32(img + 20, obj->endian, EV_CURRENT);
        elf__wr32(img + 24, obj->endian, (uint32_t)obj->entry);
        elf__wr32(img + 28, obj->endian, (uint32_t)phoff);
        elf__wr32(img + 32, obj->endian, (uint32_t)shoff);
        elf__wr32(img + 36, obj->endian, obj->flags);
        elf__wr16(img + 40, obj->endian, (uint16_t)ehsize);
        elf__wr16(img + 42, obj->endian, sizeof(Elf32_Phdr));
        elf__wr16(img + 44, obj->endian, (uint16_t)phnum);
        elf__wr16(img + 46, obj->endian, (uint16_t)shentsz);
        elf__wr16(img + 48, obj->endian, (uint16_t)sec_count);
        elf__wr16(img + 50, obj->endian, (uint16_t)shstr_index);
    } else {
        img[0] = ELFMAG0;
        img[1] = ELFMAG1;
        img[2] = ELFMAG2;
        img[3] = ELFMAG3;
        img[EI_CLASS] = ELFCLASS64;
        img[EI_DATA] = obj->endian == ELFOBJ_ENDIAN_BE ? ELFDATA2MSB : ELFDATA2LSB;
        img[EI_VERSION] = EV_CURRENT;
        img[EI_OSABI] = obj->osabi;
        img[EI_ABIVERSION] = obj->abiversion;
        elf__wr16(img + 16, obj->endian, obj->type);
        elf__wr16(img + 18, obj->endian, obj->machine);
        elf__wr32(img + 20, obj->endian, EV_CURRENT);
        elf__wr64(img + 24, obj->endian, obj->entry);
        elf__wr64(img + 32, obj->endian, phoff);
        elf__wr64(img + 40, obj->endian, shoff);
        elf__wr32(img + 48, obj->endian, obj->flags);
        elf__wr16(img + 52, obj->endian, (uint16_t)ehsize);
        elf__wr16(img + 54, obj->endian, sizeof(Elf64_Phdr));
        elf__wr16(img + 56, obj->endian, (uint16_t)phnum);
        elf__wr16(img + 58, obj->endian, (uint16_t)shentsz);
        elf__wr16(img + 60, obj->endian, (uint16_t)sec_count);
        elf__wr16(img + 62, obj->endian, (uint16_t)shstr_index);
    }

    for (i = 1; i < sec_count; ++i) {
        if (secs[i].type != SHT_NOBITS && secs[i].size > 0 && secs[i].data != NULL) {
            memcpy(img + secs[i].offset, secs[i].data, (size_t)secs[i].size);
        }
    }

    if (phnum != 0) {
        if (obj->segment_count != 0) {
            size_t phidx;
            uint64_t file_bias = 0;
            int have_file_bias = find_alloc_file_bias(secs, sec_count, &file_bias);
            for (phidx = 0; phidx < phnum; ++phidx) {
                struct elf_segment *seg = obj->segments[phidx];
                uint8_t *p = img + phoff + (phidx * phentsz);
                uint64_t lo_off = UINT64_MAX;
                uint64_t hi_off = 0;
                uint64_t lo_addr = UINT64_MAX;
                uint64_t hi_addr = 0;
                uint64_t out_filesz;
                uint64_t out_memsz;
                uint64_t out_off;
                uint64_t out_vaddr;
                size_t j;

                for (j = 0; j < seg->section_count; ++j) {
                    size_t sidx = seg->section_indices[j] + 1;
                    uint64_t end_off;
                    uint64_t end_addr;
                    if (sidx >= sec_count) {
                        continue;
                    }
                    if (secs[sidx].size == 0) {
                        continue;
                    }
                    if (secs[sidx].type != SHT_NOBITS) {
                        if (!u64_add_checked(secs[sidx].offset, secs[sidx].size, &end_off)) {
                            err = ELF_ERR_BOUNDS;
                            goto done;
                        }
                        if (secs[sidx].offset < lo_off) {
                            lo_off = secs[sidx].offset;
                        }
                        if (end_off > hi_off) {
                            hi_off = end_off;
                        }
                    }
                    if (!u64_add_checked(secs[sidx].addr, secs[sidx].size, &end_addr)) {
                        err = ELF_ERR_BOUNDS;
                        goto done;
                    }
                    if (secs[sidx].addr < lo_addr) {
                        lo_addr = secs[sidx].addr;
                    }
                    if (end_addr > hi_addr) {
                        hi_addr = end_addr;
                    }
                }

                if (seg->type == PT_PHDR) {
                    uint64_t ph_bytes = (uint64_t)phnum * (uint64_t)phentsz;
                    lo_off = phoff;
                    if (!u64_add_checked(phoff, ph_bytes, &hi_off)) {
                        err = ELF_ERR_BOUNDS;
                        goto done;
                    }
                    if (have_file_bias) {
                        if (!u64_add_checked(file_bias, phoff, &lo_addr) ||
                            !u64_add_checked(lo_addr, ph_bytes, &hi_addr)) {
                            err = ELF_ERR_BOUNDS;
                            goto done;
                        }
                    } else {
                        lo_addr = 0;
                        hi_addr = ph_bytes;
                    }
                }

                if (lo_off == UINT64_MAX) {
                    lo_off = 0;
                }
                if (lo_addr == UINT64_MAX) {
                    lo_addr = 0;
                }
                out_off = lo_off;
                out_vaddr = lo_addr;
                out_filesz = hi_off >= lo_off ? (hi_off - lo_off) : 0;
                out_memsz = hi_addr >= lo_addr ? (hi_addr - lo_addr) : 0;
                if (seg->type == PT_LOAD && (seg->flags & 0x1u) != 0 && out_off != 0) {
                    uint64_t delta = out_off;
                    if (out_vaddr < delta) {
                        err = ELF_ERR_BOUNDS;
                        goto done;
                    }
                    out_off = 0;
                    out_vaddr -= delta;
                    if (!u64_add_checked(out_filesz, delta, &out_filesz) ||
                        !u64_add_checked(out_memsz, delta, &out_memsz)) {
                        err = ELF_ERR_BOUNDS;
                        goto done;
                    }
                }

                if (obj->cls == ELFOBJ_CLASS_32) {
                    elf__wr32(p + 0, obj->endian, seg->type);
                    elf__wr32(p + 4, obj->endian, (uint32_t)out_off);
                    elf__wr32(p + 8, obj->endian, (uint32_t)out_vaddr);
                    elf__wr32(p + 12, obj->endian, (uint32_t)out_vaddr);
                    elf__wr32(p + 16, obj->endian, (uint32_t)out_filesz);
                    elf__wr32(p + 20, obj->endian, (uint32_t)out_memsz);
                    elf__wr32(p + 24, obj->endian, seg->flags);
                    elf__wr32(p + 28, obj->endian, (uint32_t)(seg->align ? seg->align : 1));
                } else {
                    elf__wr32(p + 0, obj->endian, seg->type);
                    elf__wr32(p + 4, obj->endian, seg->flags);
                    elf__wr64(p + 8, obj->endian, out_off);
                    elf__wr64(p + 16, obj->endian, out_vaddr);
                    elf__wr64(p + 24, obj->endian, out_vaddr);
                    elf__wr64(p + 32, obj->endian, out_filesz);
                    elf__wr64(p + 40, obj->endian, out_memsz);
                    elf__wr64(p + 48, obj->endian, seg->align ? seg->align : 1);
                }
            }
        } else if (obj->phdr_count != 0) {
            size_t phidx;
            uint64_t load_lo = UINT64_MAX;
            uint64_t load_hi = 0;
            uint64_t load_addr_lo = UINT64_MAX;
            uint64_t load_addr_hi = 0;
            uint64_t load_span = 0;
            uint64_t load_mem_span = 0;
            int has_load_span = 0;
            for (i = 1; i < sec_count; ++i) {
                uint64_t end;
                if ((secs[i].flags & SHF_ALLOC) == 0) {
                    continue;
                }
                if (!u64_add_checked(secs[i].addr, secs[i].size, &end)) {
                    err = ELF_ERR_BOUNDS;
                    goto done;
                }
                if (secs[i].addr < load_addr_lo) {
                    load_addr_lo = secs[i].addr;
                }
                if (end > load_addr_hi) {
                    load_addr_hi = end;
                }
                if (secs[i].type == SHT_NOBITS) {
                    continue;
                }
                if (!u64_add_checked(secs[i].offset, secs[i].size, &end)) {
                    err = ELF_ERR_BOUNDS;
                    goto done;
                }
                if (secs[i].offset < load_lo) {
                    load_lo = secs[i].offset;
                }
                if (end > load_hi) {
                    load_hi = end;
                }
                has_load_span = 1;
            }
            if (has_load_span) {
                load_span = load_hi - load_lo;
                if (load_addr_lo != UINT64_MAX) {
                    load_mem_span = load_addr_hi - load_addr_lo;
                }
            }

            for (phidx = 0; phidx < phnum; ++phidx) {
                const struct elf_phdr *ph = &obj->phdrs[phidx];
                uint8_t *p = img + phoff + (phidx * phentsz);
                uint64_t out_off = ph->offset;
                uint64_t out_vaddr = ph->vaddr;
                uint64_t out_paddr = ph->paddr;
                uint64_t out_filesz = ph->filesz;
                uint64_t out_memsz = ph->memsz;

                if (ph->type == PT_LOAD && has_load_span) {
                    out_off = load_lo;
                    out_vaddr = load_addr_lo == UINT64_MAX ? 0 : load_addr_lo;
                    out_paddr = out_vaddr;
                    out_filesz = load_span;
                    out_memsz = load_mem_span;
                }
                if (obj->cls == ELFOBJ_CLASS_32) {
                    elf__wr32(p + 0, obj->endian, ph->type);
                    elf__wr32(p + 4, obj->endian, (uint32_t)out_off);
                    elf__wr32(p + 8, obj->endian, (uint32_t)out_vaddr);
                    elf__wr32(p + 12, obj->endian, (uint32_t)out_paddr);
                    elf__wr32(p + 16, obj->endian, (uint32_t)out_filesz);
                    elf__wr32(p + 20, obj->endian, (uint32_t)out_memsz);
                    elf__wr32(p + 24, obj->endian, ph->flags);
                    elf__wr32(p + 28, obj->endian, (uint32_t)ph->align);
                } else {
                    elf__wr32(p + 0, obj->endian, ph->type);
                    elf__wr32(p + 4, obj->endian, ph->flags);
                    elf__wr64(p + 8, obj->endian, out_off);
                    elf__wr64(p + 16, obj->endian, out_vaddr);
                    elf__wr64(p + 24, obj->endian, out_paddr);
                    elf__wr64(p + 32, obj->endian, out_filesz);
                    elf__wr64(p + 40, obj->endian, out_memsz);
                    elf__wr64(p + 48, obj->endian, ph->align);
                }
            }
        } else {
            size_t phidx = 0;
            uint64_t load_lo = UINT64_MAX;
            uint64_t load_hi = 0;
            uint64_t load_addr_lo = UINT64_MAX;
            uint64_t load_addr_hi = 0;
            uint32_t load_flags = 0;
            int has_load = 0;

            for (i = 1; i < sec_count; ++i) {
                uint64_t end;
                if ((secs[i].flags & SHF_ALLOC) == 0) {
                    continue;
                }
                if (!u64_add_checked(secs[i].addr, secs[i].size, &end)) {
                    err = ELF_ERR_BOUNDS;
                    goto done;
                }
                if (secs[i].addr < load_addr_lo) {
                    load_addr_lo = secs[i].addr;
                }
                if (end > load_addr_hi) {
                    load_addr_hi = end;
                }
                if (secs[i].type == SHT_NOBITS) {
                    continue;
                }
                if (!u64_add_checked(secs[i].offset, secs[i].size, &end)) {
                    err = ELF_ERR_BOUNDS;
                    goto done;
                }
                if (secs[i].offset < load_lo) {
                    load_lo = secs[i].offset;
                }
                if (end > load_hi) {
                    load_hi = end;
                }
                if (secs[i].flags & SHF_EXECINSTR) {
                    load_flags |= 0x1;
                }
                if (secs[i].flags & SHF_WRITE) {
                    load_flags |= 0x2;
                }
                load_flags |= 0x4;
                has_load = 1;
            }
            if (has_load && phidx < phnum) {
                uint8_t *p = img + phoff + (phidx * phentsz);
                uint64_t filesz = load_hi - load_lo;
                uint64_t memsz = load_addr_hi - load_addr_lo;
                if (obj->cls == ELFOBJ_CLASS_32) {
                    elf__wr32(p + 0, obj->endian, PT_LOAD);
                    elf__wr32(p + 4, obj->endian, (uint32_t)load_lo);
                    elf__wr32(p + 8, obj->endian, (uint32_t)(load_addr_lo == UINT64_MAX ? 0 : load_addr_lo));
                    elf__wr32(p + 12, obj->endian, (uint32_t)(load_addr_lo == UINT64_MAX ? 0 : load_addr_lo));
                    elf__wr32(p + 16, obj->endian, (uint32_t)filesz);
                    elf__wr32(p + 20, obj->endian, (uint32_t)memsz);
                    elf__wr32(p + 24, obj->endian, load_flags);
                    elf__wr32(p + 28, obj->endian, 0x1000);
                } else {
                    elf__wr32(p + 0, obj->endian, PT_LOAD);
                    elf__wr32(p + 4, obj->endian, load_flags);
                    elf__wr64(p + 8, obj->endian, load_lo);
                    elf__wr64(p + 16, obj->endian, load_addr_lo == UINT64_MAX ? 0 : load_addr_lo);
                    elf__wr64(p + 24, obj->endian, load_addr_lo == UINT64_MAX ? 0 : load_addr_lo);
                    elf__wr64(p + 32, obj->endian, filesz);
                    elf__wr64(p + 40, obj->endian, memsz);
                    elf__wr64(p + 48, obj->endian, 0x1000);
                }
                phidx++;
            }
            for (i = 1; i < sec_count && phidx < phnum; ++i) {
                uint32_t ptype = 0;
                uint32_t pflags = 0x4;
                uint8_t *p;
                if (secs[i].type == SHT_DYNAMIC) {
                    ptype = PT_DYNAMIC;
                    pflags = 0x6;
                } else if (strcmp(secs[i].name ? secs[i].name : "", ".interp") == 0) {
                    ptype = PT_INTERP;
                } else if (secs[i].flags & SHF_TLS) {
                    ptype = PT_TLS;
                } else if (obj->machine == EM_ARM && secs[i].type == SHT_ARM_EXIDX) {
                    ptype = PT_ARM_EXIDX;
                }
                if (ptype == 0) {
                    continue;
                }
                p = img + phoff + (phidx * phentsz);
                if (obj->cls == ELFOBJ_CLASS_32) {
                    elf__wr32(p + 0, obj->endian, ptype);
                    elf__wr32(p + 4, obj->endian, (uint32_t)secs[i].offset);
                    elf__wr32(p + 8, obj->endian, (uint32_t)secs[i].addr);
                    elf__wr32(p + 12, obj->endian, (uint32_t)secs[i].addr);
                    elf__wr32(p + 16, obj->endian, (uint32_t)secs[i].size);
                    elf__wr32(p + 20, obj->endian, (uint32_t)secs[i].size);
                    elf__wr32(p + 24, obj->endian, pflags);
                    elf__wr32(p + 28, obj->endian, (uint32_t)(secs[i].addralign ? secs[i].addralign : 1));
                } else {
                    elf__wr32(p + 0, obj->endian, ptype);
                    elf__wr32(p + 4, obj->endian, pflags);
                    elf__wr64(p + 8, obj->endian, secs[i].offset);
                    elf__wr64(p + 16, obj->endian, secs[i].addr);
                    elf__wr64(p + 24, obj->endian, secs[i].addr);
                    elf__wr64(p + 32, obj->endian, secs[i].size);
                    elf__wr64(p + 40, obj->endian, secs[i].size);
                    elf__wr64(p + 48, obj->endian, secs[i].addralign ? secs[i].addralign : 1);
                }
                phidx++;
            }
        }
    }

    for (i = 0; i < sec_count; ++i) {
        uint8_t *p = img + shoff + (i * shentsz);
        if (obj->cls == ELFOBJ_CLASS_32) {
            elf__wr32(p + 0, obj->endian, secs[i].sh_name);
            elf__wr32(p + 4, obj->endian, secs[i].type);
            elf__wr32(p + 8, obj->endian, (uint32_t)secs[i].flags);
            elf__wr32(p + 12, obj->endian, (uint32_t)secs[i].addr);
            elf__wr32(p + 16, obj->endian, (uint32_t)secs[i].offset);
            elf__wr32(p + 20, obj->endian, (uint32_t)secs[i].size);
            elf__wr32(p + 24, obj->endian, secs[i].link);
            elf__wr32(p + 28, obj->endian, secs[i].info);
            elf__wr32(p + 32, obj->endian, (uint32_t)secs[i].addralign);
            elf__wr32(p + 36, obj->endian, (uint32_t)secs[i].entsize);
        } else {
            elf__wr32(p + 0, obj->endian, secs[i].sh_name);
            elf__wr32(p + 4, obj->endian, secs[i].type);
            elf__wr64(p + 8, obj->endian, secs[i].flags);
            elf__wr64(p + 16, obj->endian, secs[i].addr);
            elf__wr64(p + 24, obj->endian, secs[i].offset);
            elf__wr64(p + 32, obj->endian, secs[i].size);
            elf__wr32(p + 40, obj->endian, secs[i].link);
            elf__wr32(p + 44, obj->endian, secs[i].info);
            elf__wr64(p + 48, obj->endian, secs[i].addralign);
            elf__wr64(p + 56, obj->endian, secs[i].entsize);
        }
    }

    *out_buf = img;
    err = ELF_OK;

done:
    if (err != ELF_OK) {
        free(img);
    }
    free(group_sig_indices);
    free(group_sig_names);
    free(obj_sec_to_out);
    free(ver_names);
    free(versym_data);
    free(verdef_data);
    free_out_groups(groups, group_count);
    for (i = 0; i < sec_count; ++i) {
        if (secs[i].owns_data) {
            free((void *)secs[i].data);
        }
        if (secs[i].owns_name) {
            free((void *)secs[i].name);
        }
    }
    free(secs);
    elf__strtab_free(&shstr);
    elf__strtab_free(&strtab);
    return err;
}

elf_err_t elf_write_file(elfobj_t *obj, const char *path) {
    uint8_t *buf = NULL;
    size_t size = 0;
    elf_err_t err;

    if (obj == NULL || path == NULL) {
        return ELF_ERR_STATE;
    }

    if (obj->readonly && obj->dirty == 0 && obj->image != NULL) {
        return elf__write_file_atomic(path, obj->image, obj->image_size);
    }

    if (!obj->finalized) {
        err = elf_finalize(obj);
        if (err != ELF_OK) {
            return err;
        }
    }

    err = elf__write_to_buffer(obj, &buf, &size);
    if (err != ELF_OK) {
        return err;
    }

    err = elf__write_file_atomic(path, buf, size);
    if (err != ELF_OK) {
        free(buf);
        return err;
    }
    free(buf);
    obj->dirty = 0;
    return ELF_OK;
}
