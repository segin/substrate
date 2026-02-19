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
                                         size_t *out_size, size_t *out_entsize) {
    size_t i;
    size_t n = target->reloc_count;
    size_t entsz = obj->cls == ELFOBJ_CLASS_64 ? 24 : 12;
    uint8_t *buf;

    if (n == 0) {
        *out_size = 0;
        *out_entsize = entsz;
        return NULL;
    }

    buf = (uint8_t *)elf__calloc(n, entsz);
    if (buf == NULL) {
        return NULL;
    }

    for (i = 0; i < n; ++i) {
        const struct elf_reloc *r = target->relocs[i];
        size_t sym_index = 0;
        uint8_t *p = buf + (i * entsz);

        if (r->symbol != NULL) {
            sym_index = r->symbol->index + 1;
        }

        if (obj->cls == ELFOBJ_CLASS_32) {
            uint32_t info = ELF32_R_INFO((uint32_t)sym_index, r->type);
            elf__wr32(p + 0, obj->endian, (uint32_t)r->offset);
            elf__wr32(p + 4, obj->endian, info);
            elf__wr32(p + 8, obj->endian, (uint32_t)r->addend);
        } else {
            uint64_t info = ELF64_R_INFO(sym_index, r->type);
            elf__wr64(p + 0, obj->endian, r->offset);
            elf__wr64(p + 8, obj->endian, info);
            elf__wr64(p + 16, obj->endian, (uint64_t)r->addend);
        }
    }

    *out_size = n * entsz;
    *out_entsize = entsz;
    return buf;
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
    size_t symtab_size = 0;
    size_t symtab_entsz = 0;
    uint64_t shoff;
    uint64_t off;
    uint8_t *img = NULL;
    size_t ehsize = obj->cls == ELFOBJ_CLASS_64 ? sizeof(Elf64_Ehdr) : sizeof(Elf32_Ehdr);
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
        out_sec_t out;
        memset(&out, 0, sizeof(out));
        out.name = s->name ? s->name : "";
        out.type = s->type;
        out.flags = s->flags;
        out.addr = s->addr;
        out.link = s->link;
        out.info = s->info;
        out.addralign = s->addralign ? s->addralign : 1;
        out.entsize = s->entsize;
        out.data = s->data;
        out.size = s->type == SHT_NOBITS ? s->size : s->data_size;
        if (out_push(&secs, &sec_count, &sec_cap, &out) != ELF_OK) {
            err = ELF_ERR_OOM;
            goto done;
        }
    }

    for (i = 0; i < obj->section_count; ++i) {
        const struct elf_section *target = obj->sections[i];
        uint8_t *rel_data;
        size_t rel_size;
        size_t rel_ent = 0;
        out_sec_t out;
        char *name;
        size_t nlen;

        if (target->reloc_count == 0) {
            continue;
        }

        rel_data = build_relocs_for_section(obj, target, &rel_size, &rel_ent);
        if (rel_data == NULL && rel_size != 0) {
            err = ELF_ERR_OOM;
            goto done;
        }

        nlen = strlen(target->name ? target->name : "") + 6;
        name = (char *)malloc(nlen);
        if (name == NULL) {
            free(rel_data);
            err = ELF_ERR_OOM;
            goto done;
        }
        memcpy(name, ".rela", 5);
        memcpy(name + 5, target->name ? target->name : "", nlen - 5);

        memset(&out, 0, sizeof(out));
        out.name = name;
        out.type = SHT_RELA;
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
    secs[symtab_index].info = 1;

    for (i = 1; i < sec_count; ++i) {
        if (secs[i].type == SHT_RELA || secs[i].type == SHT_REL) {
            secs[i].link = (uint32_t)symtab_index;
        }
    }

    off = ehsize;
    for (i = 1; i < sec_count; ++i) {
        if (secs[i].type == SHT_NOBITS) {
            secs[i].offset = 0;
            continue;
        }
        off = align_up(off, secs[i].addralign ? secs[i].addralign : 1);
        secs[i].offset = off;
        off += secs[i].size;
    }

    shoff = align_up(off, obj->cls == ELFOBJ_CLASS_64 ? 8 : 4);
    *out_sz = (size_t)(shoff + (sec_count * shentsz));
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
        elf__wr16(img + 16, obj->endian, obj->type);
        elf__wr16(img + 18, obj->endian, obj->machine);
        elf__wr32(img + 20, obj->endian, EV_CURRENT);
        elf__wr32(img + 24, obj->endian, (uint32_t)obj->entry);
        elf__wr32(img + 28, obj->endian, 0);
        elf__wr32(img + 32, obj->endian, (uint32_t)shoff);
        elf__wr32(img + 36, obj->endian, obj->flags);
        elf__wr16(img + 40, obj->endian, (uint16_t)ehsize);
        elf__wr16(img + 42, obj->endian, sizeof(Elf32_Phdr));
        elf__wr16(img + 44, obj->endian, 0);
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
        elf__wr16(img + 16, obj->endian, obj->type);
        elf__wr16(img + 18, obj->endian, obj->machine);
        elf__wr32(img + 20, obj->endian, EV_CURRENT);
        elf__wr64(img + 24, obj->endian, obj->entry);
        elf__wr64(img + 32, obj->endian, 0);
        elf__wr64(img + 40, obj->endian, shoff);
        elf__wr32(img + 48, obj->endian, obj->flags);
        elf__wr16(img + 52, obj->endian, (uint16_t)ehsize);
        elf__wr16(img + 54, obj->endian, sizeof(Elf64_Phdr));
        elf__wr16(img + 56, obj->endian, 0);
        elf__wr16(img + 58, obj->endian, (uint16_t)shentsz);
        elf__wr16(img + 60, obj->endian, (uint16_t)sec_count);
        elf__wr16(img + 62, obj->endian, (uint16_t)shstr_index);
    }

    for (i = 1; i < sec_count; ++i) {
        if (secs[i].type != SHT_NOBITS && secs[i].size > 0 && secs[i].data != NULL) {
            memcpy(img + secs[i].offset, secs[i].data, (size_t)secs[i].size);
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
    return ELF_OK;
}
