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
} out_sec_t;

static uint64_t align_up(uint64_t v, uint64_t a) {
    if (a <= 1) {
        return v;
    }
    return (v + (a - 1)) & ~(a - 1);
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

static uint8_t *build_symtab(const elfobj_t *obj, elfobj_endian_t e, elfobj_class_t cls,
                             elf_strtab_t *strtab, size_t *out_size, size_t *out_entsize) {
    size_t entsz = (cls == ELFOBJ_CLASS_64) ? 24 : 16;
    size_t n = obj->symbol_count + 1;
    uint8_t *buf = (uint8_t *)elf__calloc(n, entsz);
    size_t i;

    if (buf == NULL) {
        return NULL;
    }

    for (i = 0; i < obj->symbol_count; ++i) {
        const struct elf_symbol *sym = obj->symbols[i];
        uint8_t *p = buf + ((i + 1) * entsz);
        uint32_t st_name = elf__strtab_add(strtab, sym->name ? sym->name : "");
        uint8_t st_info = ELF32_ST_INFO(sym->bind, sym->type);
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

    *out_size = n * entsz;
    *out_entsize = entsz;
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
    size_t sec_count = 0;
    size_t sec_cap = 0;
    elf_strtab_t shstr;
    elf_strtab_t strtab;
    size_t symtab_index = 0;
    size_t strtab_index = 0;
    size_t shstr_index = 0;
    size_t i;
    uint8_t *symtab_data = NULL;
    uint8_t *dynstr_data = NULL;
    size_t dynstr_size = 0;
    int has_dynstr_section = 0;
    size_t symtab_size = 0;
    size_t symtab_entsz = 0;
    uint64_t shoff;
    uint64_t off;
    uint64_t phoff = 0;
    uint8_t *img = NULL;
    size_t ehsize = obj->cls == ELFOBJ_CLASS_64 ? sizeof(Elf64_Ehdr) : sizeof(Elf32_Ehdr);
    size_t phentsz = obj->cls == ELFOBJ_CLASS_64 ? sizeof(Elf64_Phdr) : sizeof(Elf32_Phdr);
    size_t phnum = 0;
    size_t shentsz = obj->cls == ELFOBJ_CLASS_64 ? 64 : 40;
    elf_err_t err;

    if (obj == NULL || out_buf == NULL || out_sz == NULL) {
        return ELF_ERR_STATE;
    }

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

    for (i = 0; i < obj->section_count; ++i) {
        const struct elf_section *s = obj->sections[i];
        const char *name = s->name ? s->name : "";
        out_sec_t out;

        if (strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 ||
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
        if (strcmp(name, ".dynstr") == 0) {
            has_dynstr_section = 1;
        }
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
            out.info = (uint32_t)(i + 1);
            if (out_push(&secs, &sec_count, &sec_cap, &out) != ELF_OK) {
                free(name);
                free(rel_data);
                err = ELF_ERR_OOM;
                goto done;
            }
        }
    }

    symtab_data = build_symtab(obj, obj->endian, obj->cls, &strtab, &symtab_size, &symtab_entsz);
    if (symtab_data == NULL) {
        err = ELF_ERR_OOM;
        goto done;
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
        size_t first_global = obj->symbol_count + 1;
        for (i = 0; i < obj->symbol_count; ++i) {
            if (obj->symbols[i]->bind != STB_LOCAL) {
                first_global = i + 1;
                break;
            }
        }
        secs[symtab_index].info = (uint32_t)first_global;
    }

    for (i = 1; i < sec_count; ++i) {
        if (secs[i].type == SHT_RELA || secs[i].type == SHT_REL) {
            secs[i].link = (uint32_t)symtab_index;
        }
    }
    {
        size_t dynsym_index = (size_t)-1;
        size_t dynstr_index = (size_t)-1;
        size_t dynamic_index = (size_t)-1;
        size_t hash_index = (size_t)-1;
        size_t gnu_hash_index = (size_t)-1;
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
        off = align_up(off, secs[i].addralign ? secs[i].addralign : 1);
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
                if (seg->type == PT_LOAD && (seg->flags & 0x1u) != 0 && phoff < out_off) {
                    uint64_t delta = out_off - phoff;
                    if (out_vaddr < delta) {
                        err = ELF_ERR_BOUNDS;
                        goto done;
                    }
                    out_off = phoff;
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
    FILE *fp;
    uint8_t *buf = NULL;
    size_t size = 0;
    elf_err_t err;

    if (obj == NULL || path == NULL) {
        return ELF_ERR_STATE;
    }

    if (obj->readonly && obj->dirty == 0 && obj->image != NULL) {
        fp = fopen(path, "wb");
        if (fp == NULL) {
            return ELF_ERR_IO;
        }
        if (fwrite(obj->image, 1, obj->image_size, fp) != obj->image_size) {
            fclose(fp);
            return ELF_ERR_IO;
        }
        fclose(fp);
        return ELF_OK;
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

    fp = fopen(path, "wb");
    if (fp == NULL) {
        free(buf);
        return ELF_ERR_IO;
    }
    if (fwrite(buf, 1, size, fp) != size) {
        fclose(fp);
        free(buf);
        return ELF_ERR_IO;
    }
    fclose(fp);
    free(buf);
    obj->dirty = 0;
    return ELF_OK;
}
