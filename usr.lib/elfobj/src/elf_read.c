#include "elf_private.h"

typedef struct {
    size_t sec_index;
    struct elf_symbol **symbols;
    size_t count;
} symtab_index_t;

static int is_elf_magic(const uint8_t *buf, size_t size) {
    if (size < EI_NIDENT) {
        return 0;
    }
    return buf[0] == ELFMAG0 && buf[1] == ELFMAG1 && buf[2] == ELFMAG2 && buf[3] == ELFMAG3;
}

static const char *safe_str(const uint8_t *base, size_t len, uint32_t off) {
    size_t i;
    if (base == NULL || off >= len) {
        return NULL;
    }
    for (i = off; i < len; ++i) {
        if (base[i] == '\0') {
            return (const char *)(base + off);
        }
    }
    return NULL;
}

static int ranges_overlap_u64(uint64_t a_off, uint64_t a_sz, uint64_t b_off, uint64_t b_sz) {
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

static struct elf_section *parse_shdr32(elfobj_t *obj, const uint8_t *p) {
    struct elf_section *sec = (struct elf_section *)elf__calloc(1, sizeof(*sec));
    if (sec == NULL) {
        return NULL;
    }
    sec->obj = obj;
    sec->sh_name = elf__rd32(p + 0, obj->endian);
    sec->type = elf__rd32(p + 4, obj->endian);
    sec->flags = elf__rd32(p + 8, obj->endian);
    sec->addr = elf__rd32(p + 12, obj->endian);
    sec->offset = elf__rd32(p + 16, obj->endian);
    sec->size = elf__rd32(p + 20, obj->endian);
    sec->link = elf__rd32(p + 24, obj->endian);
    sec->info = elf__rd32(p + 28, obj->endian);
    sec->addralign = elf__rd32(p + 32, obj->endian);
    sec->entsize = elf__rd32(p + 36, obj->endian);
    sec->addralign = sec->addralign == 0 ? 1 : sec->addralign;
    sec->name = elf__strdup("");
    return sec;
}

static struct elf_section *parse_shdr64(elfobj_t *obj, const uint8_t *p) {
    struct elf_section *sec = (struct elf_section *)elf__calloc(1, sizeof(*sec));
    if (sec == NULL) {
        return NULL;
    }
    sec->obj = obj;
    sec->sh_name = elf__rd32(p + 0, obj->endian);
    sec->type = elf__rd32(p + 4, obj->endian);
    sec->flags = elf__rd64(p + 8, obj->endian);
    sec->addr = elf__rd64(p + 16, obj->endian);
    sec->offset = elf__rd64(p + 24, obj->endian);
    sec->size = elf__rd64(p + 32, obj->endian);
    sec->link = elf__rd32(p + 40, obj->endian);
    sec->info = elf__rd32(p + 44, obj->endian);
    sec->addralign = elf__rd64(p + 48, obj->endian);
    sec->entsize = elf__rd64(p + 56, obj->endian);
    sec->addralign = sec->addralign == 0 ? 1 : sec->addralign;
    sec->name = elf__strdup("");
    return sec;
}

static elf_err_t parse_sections(elfobj_t *obj, uint64_t shoff, uint16_t entsize, uint16_t shnum) {
    size_t i;
    uint64_t table_size;
    if (shnum == 0) {
        return ELF_OK;
    }
    if (!elf__u64_mul((uint64_t)entsize, (uint64_t)shnum, &table_size)) {
        return ELF_ERR_BOUNDS;
    }
    if (shoff > SIZE_MAX || table_size > SIZE_MAX) {
        return ELF_ERR_BOUNDS;
    }
    if (!elf__bounds_ok((size_t)shoff, (size_t)table_size, obj->image_size)) {
        return ELF_ERR_BOUNDS;
    }

    for (i = 0; i < shnum; ++i) {
        const uint8_t *p = obj->image + shoff + ((size_t)entsize * i);
        struct elf_section *sec;

        if (obj->cls == ELFOBJ_CLASS_32) {
            if (entsize < 40) {
                return ELF_ERR_FORMAT;
            }
            sec = parse_shdr32(obj, p);
        } else {
            if (entsize < 64) {
                return ELF_ERR_FORMAT;
            }
            sec = parse_shdr64(obj, p);
        }

        if (sec == NULL) {
            return ELF_ERR_OOM;
        }

        if (sec->type != SHT_NOBITS && sec->size > 0) {
            size_t j;
            if (!elf__bounds_ok((size_t)sec->offset, (size_t)sec->size, obj->image_size)) {
                free(sec->name);
                free(sec);
                return ELF_ERR_BOUNDS;
            }
            for (j = 0; j < obj->section_count; ++j) {
                struct elf_section *prev = obj->sections[j];
                if (prev == NULL || prev->type == SHT_NOBITS || prev->size == 0) {
                    continue;
                }
                if (ranges_overlap_u64(prev->offset, prev->size, sec->offset, sec->size)) {
                    free(sec->name);
                    free(sec);
                    return ELF_ERR_FORMAT;
                }
            }
            sec->data = obj->image + sec->offset;
            sec->data_size = (size_t)sec->size;
            sec->owns_data = 0;
        }

        if (elf__push_section(obj, sec) != ELF_OK) {
            free(sec->name);
            free(sec);
            return ELF_ERR_OOM;
        }
    }

    return ELF_OK;
}

static elf_err_t parse_program_headers(elfobj_t *obj, uint64_t phoff, uint16_t entsize, uint16_t phnum) {
    size_t i;
    uint64_t table_size;

    if (phnum == 0) {
        return ELF_OK;
    }
    if (entsize == 0) {
        return ELF_ERR_FORMAT;
    }
    if (!elf__u64_mul((uint64_t)entsize, (uint64_t)phnum, &table_size)) {
        return ELF_ERR_BOUNDS;
    }
    if (phoff > SIZE_MAX || table_size > SIZE_MAX) {
        return ELF_ERR_BOUNDS;
    }
    if (!elf__bounds_ok((size_t)phoff, (size_t)table_size, obj->image_size)) {
        return ELF_ERR_BOUNDS;
    }

    for (i = 0; i < phnum; ++i) {
        const uint8_t *p = obj->image + phoff + ((size_t)entsize * i);
        struct elf_phdr phdr;
        memset(&phdr, 0, sizeof(phdr));

        if (obj->cls == ELFOBJ_CLASS_32) {
            if (entsize < sizeof(Elf32_Phdr)) {
                return ELF_ERR_FORMAT;
            }
            phdr.type = elf__rd32(p + 0, obj->endian);
            phdr.offset = elf__rd32(p + 4, obj->endian);
            phdr.vaddr = elf__rd32(p + 8, obj->endian);
            phdr.paddr = elf__rd32(p + 12, obj->endian);
            phdr.filesz = elf__rd32(p + 16, obj->endian);
            phdr.memsz = elf__rd32(p + 20, obj->endian);
            phdr.flags = elf__rd32(p + 24, obj->endian);
            phdr.align = elf__rd32(p + 28, obj->endian);
        } else {
            if (entsize < sizeof(Elf64_Phdr)) {
                return ELF_ERR_FORMAT;
            }
            phdr.type = elf__rd32(p + 0, obj->endian);
            phdr.flags = elf__rd32(p + 4, obj->endian);
            phdr.offset = elf__rd64(p + 8, obj->endian);
            phdr.vaddr = elf__rd64(p + 16, obj->endian);
            phdr.paddr = elf__rd64(p + 24, obj->endian);
            phdr.filesz = elf__rd64(p + 32, obj->endian);
            phdr.memsz = elf__rd64(p + 40, obj->endian);
            phdr.align = elf__rd64(p + 48, obj->endian);
        }

        if ((phdr.offset > SIZE_MAX || phdr.filesz > SIZE_MAX) &&
            phdr.filesz > 0) {
            return ELF_ERR_BOUNDS;
        }
        if (phdr.filesz > 0 && !elf__bounds_ok((size_t)phdr.offset, (size_t)phdr.filesz, obj->image_size)) {
            return ELF_ERR_BOUNDS;
        }
        if (elf__push_phdr(obj, &phdr) != ELF_OK) {
            return ELF_ERR_OOM;
        }
    }

    return ELF_OK;
}

static void detect_special_sections(elfobj_t *obj) {
    size_t i;
    for (i = 0; i < obj->section_count; ++i) {
        const struct elf_section *s = obj->sections[i];
        if (s == NULL) {
            continue;
        }
        if (s->type == SHT_NOTE) {
            obj->has_notes = 1;
        } else if (s->type == SHT_DYNAMIC) {
            obj->has_dynamic = 1;
        } else if (s->type == 0x6ffffffd || s->type == 0x6ffffffe || s->type == 0x6fffffff) {
            obj->has_versioning = 1;
        }
    }
}

static elf_err_t resolve_section_names(elfobj_t *obj) {
    size_t i;
    struct elf_section *strsec;

    if (obj->shstrndx >= obj->section_count) {
        return ELF_ERR_FORMAT;
    }
    strsec = obj->sections[obj->shstrndx];
    if (strsec == NULL || strsec->type != SHT_STRTAB || strsec->data == NULL) {
        return ELF_ERR_FORMAT;
    }

    for (i = 0; i < obj->section_count; ++i) {
        const char *name = safe_str(strsec->data, strsec->data_size, obj->sections[i]->sh_name);
        char *dup = elf__strdup(name == NULL ? "" : name);
        if (dup == NULL) {
            return ELF_ERR_OOM;
        }
        free(obj->sections[i]->name);
        obj->sections[i]->name = dup;
    }

    return ELF_OK;
}

static symtab_index_t *find_symtab(symtab_index_t *maps, size_t n, size_t sec_index) {
    size_t i;
    for (i = 0; i < n; ++i) {
        if (maps[i].sec_index == sec_index) {
            return &maps[i];
        }
    }
    return NULL;
}

static elf_err_t parse_symbols(elfobj_t *obj, symtab_index_t **out_maps, size_t *out_count) {
    size_t i;
    size_t map_count = 0;
    symtab_index_t *maps = NULL;

    for (i = 0; i < obj->section_count; ++i) {
        struct elf_section *sec = obj->sections[i];
        size_t nsyms;
        size_t j;
        size_t entsz;
        struct elf_section *strsec;
        symtab_index_t map;

        if (sec->type != SHT_SYMTAB && sec->type != SHT_DYNSYM) {
            continue;
        }
        if (sec->data == NULL) {
            continue;
        }

        entsz = (size_t)(sec->entsize ? sec->entsize : (obj->cls == ELFOBJ_CLASS_32 ? 16 : 24));
        if (entsz == 0 || sec->data_size % entsz != 0) {
            free(maps);
            return ELF_ERR_FORMAT;
        }
        nsyms = sec->data_size / entsz;

        if (sec->link >= obj->section_count) {
            free(maps);
            return ELF_ERR_FORMAT;
        }
        strsec = obj->sections[sec->link];
        if (strsec->type != SHT_STRTAB || strsec->data == NULL) {
            free(maps);
            return ELF_ERR_FORMAT;
        }

        map.sec_index = i;
        map.count = nsyms;
        map.symbols = (struct elf_symbol **)elf__calloc(nsyms, sizeof(map.symbols[0]));
        if (map.symbols == NULL && nsyms != 0) {
            free(maps);
            return ELF_ERR_OOM;
        }

        for (j = 0; j < nsyms; ++j) {
            const uint8_t *p = sec->data + (j * entsz);
            uint32_t st_name;
            uint64_t st_value;
            uint64_t st_size;
            uint8_t st_info;
            uint8_t st_other;
            uint16_t st_shndx;
            const char *name;
            struct elf_symbol *sym;

            if (obj->cls == ELFOBJ_CLASS_32) {
                st_name = elf__rd32(p + 0, obj->endian);
                st_value = elf__rd32(p + 4, obj->endian);
                st_size = elf__rd32(p + 8, obj->endian);
                st_info = p[12];
                st_other = p[13];
                st_shndx = elf__rd16(p + 14, obj->endian);
            } else {
                st_name = elf__rd32(p + 0, obj->endian);
                st_info = p[4];
                st_other = p[5];
                st_shndx = elf__rd16(p + 6, obj->endian);
                st_value = elf__rd64(p + 8, obj->endian);
                st_size = elf__rd64(p + 16, obj->endian);
            }

            name = safe_str(strsec->data, strsec->data_size, st_name);
            sym = (struct elf_symbol *)elf__calloc(1, sizeof(*sym));
            if (sym == NULL) {
                size_t k;
                for (k = 0; k < map_count; ++k) {
                    free(maps[k].symbols);
                }
                free(map.symbols);
                free(maps);
                return ELF_ERR_OOM;
            }

            sym->obj = obj;
            sym->name = elf__strdup(name == NULL ? "" : name);
            sym->value = st_value;
            sym->size = st_size;
            sym->bind = ELF32_ST_BIND(st_info);
            sym->type = ELF32_ST_TYPE(st_info);
            sym->other = st_other;
            sym->shndx = st_shndx;

            if (sym->name == NULL || elf__push_symbol(obj, sym) != ELF_OK) {
                free(sym->name);
                free(sym);
                size_t k;
                for (k = 0; k < map_count; ++k) {
                    free(maps[k].symbols);
                }
                free(map.symbols);
                free(maps);
                return ELF_ERR_OOM;
            }
            map.symbols[j] = sym;
        }

        {
            void *next = elf__reallocarray(maps, map_count + 1, sizeof(maps[0]));
            if (next == NULL) {
                size_t k;
                for (k = 0; k < map_count; ++k) {
                    free(maps[k].symbols);
                }
                free(map.symbols);
                free(maps);
                return ELF_ERR_OOM;
            }
            maps = (symtab_index_t *)next;
        }
        maps[map_count++] = map;
    }

    *out_maps = maps;
    *out_count = map_count;
    return ELF_OK;
}

static elf_err_t parse_relocations(elfobj_t *obj, symtab_index_t *maps, size_t map_count) {
    size_t i;

    for (i = 0; i < obj->section_count; ++i) {
        struct elf_section *sec = obj->sections[i];
        struct elf_section *target;
        symtab_index_t *map;
        size_t entsz;
        size_t nrel;
        size_t j;

        if (sec->type != SHT_REL && sec->type != SHT_RELA) {
            continue;
        }
        if (sec->data == NULL) {
            continue;
        }

        if (sec->link >= obj->section_count || sec->info >= obj->section_count) {
            return ELF_ERR_FORMAT;
        }
        target = obj->sections[sec->info];
        map = find_symtab(maps, map_count, sec->link);
        if (map == NULL) {
            return ELF_ERR_FORMAT;
        }

        if (obj->cls == ELFOBJ_CLASS_32) {
            entsz = sec->type == SHT_RELA ? 12 : 8;
        } else {
            entsz = sec->type == SHT_RELA ? 24 : 16;
        }
        if (sec->entsize != 0) {
            entsz = (size_t)sec->entsize;
        }
        if (entsz == 0 || sec->data_size % entsz != 0) {
            return ELF_ERR_FORMAT;
        }
        nrel = sec->data_size / entsz;

        for (j = 0; j < nrel; ++j) {
            const uint8_t *p = sec->data + (j * entsz);
            uint64_t r_offset;
            uint64_t r_info;
            int64_t addend = 0;
            uint64_t sym_index;
            uint32_t r_type;
            struct elf_reloc *rel;

            if (obj->cls == ELFOBJ_CLASS_32) {
                r_offset = elf__rd32(p + 0, obj->endian);
                r_info = elf__rd32(p + 4, obj->endian);
                if (sec->type == SHT_RELA) {
                    addend = (int32_t)elf__rd32(p + 8, obj->endian);
                }
                sym_index = ELF32_R_SYM((uint32_t)r_info);
                r_type = ELF32_R_TYPE((uint32_t)r_info);
            } else {
                r_offset = elf__rd64(p + 0, obj->endian);
                r_info = elf__rd64(p + 8, obj->endian);
                if (sec->type == SHT_RELA) {
                    addend = (int64_t)elf__rd64(p + 16, obj->endian);
                }
                sym_index = ELF64_R_SYM(r_info);
                r_type = (uint32_t)ELF64_R_TYPE(r_info);
            }

            rel = (struct elf_reloc *)elf__calloc(1, sizeof(*rel));
            if (rel == NULL) {
                return ELF_ERR_OOM;
            }
            rel->section = target;
            rel->offset = r_offset;
            rel->type = r_type;
            rel->addend = addend;
            rel->has_addend = sec->type == SHT_RELA;
            if (sym_index < map->count) {
                rel->symbol = map->symbols[sym_index];
            }

            if (elf__push_reloc(obj, rel) != ELF_OK) {
                free(rel);
                return ELF_ERR_OOM;
            }
        }
    }

    return ELF_OK;
}

static elf_err_t parse_object(elfobj_t *obj) {
    const uint8_t *b = obj->image;
    uint64_t phoff;
    uint64_t shoff;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    symtab_index_t *maps = NULL;
    size_t map_count = 0;
    size_t i;
    elf_err_t err;

    if (!is_elf_magic(b, obj->image_size)) {
        return ELF_ERR_FORMAT;
    }

    obj->cls = b[EI_CLASS] == ELFCLASS64 ? ELFOBJ_CLASS_64 : ELFOBJ_CLASS_32;
    if (b[EI_CLASS] != ELFCLASS32 && b[EI_CLASS] != ELFCLASS64) {
        return ELF_ERR_FORMAT;
    }
    if (b[EI_DATA] == ELFDATA2LSB) {
        obj->endian = ELFOBJ_ENDIAN_LE;
    } else if (b[EI_DATA] == ELFDATA2MSB) {
        obj->endian = ELFOBJ_ENDIAN_BE;
    } else {
        return ELF_ERR_FORMAT;
    }

    if (obj->cls == ELFOBJ_CLASS_32) {
        if (obj->image_size < sizeof(Elf32_Ehdr)) {
            return ELF_ERR_BOUNDS;
        }
        obj->type = elf__rd16(b + 16, obj->endian);
        obj->machine = elf__rd16(b + 18, obj->endian);
        obj->entry = elf__rd32(b + 24, obj->endian);
        phoff = elf__rd32(b + 28, obj->endian);
        shoff = elf__rd32(b + 32, obj->endian);
        obj->flags = elf__rd32(b + 36, obj->endian);
        phentsize = elf__rd16(b + 42, obj->endian);
        phnum = elf__rd16(b + 44, obj->endian);
        shentsize = elf__rd16(b + 46, obj->endian);
        shnum = elf__rd16(b + 48, obj->endian);
        obj->shstrndx = elf__rd16(b + 50, obj->endian);
    } else {
        if (obj->image_size < sizeof(Elf64_Ehdr)) {
            return ELF_ERR_BOUNDS;
        }
        obj->type = elf__rd16(b + 16, obj->endian);
        obj->machine = elf__rd16(b + 18, obj->endian);
        obj->entry = elf__rd64(b + 24, obj->endian);
        phoff = elf__rd64(b + 32, obj->endian);
        shoff = elf__rd64(b + 40, obj->endian);
        obj->flags = elf__rd32(b + 48, obj->endian);
        phentsize = elf__rd16(b + 54, obj->endian);
        phnum = elf__rd16(b + 56, obj->endian);
        shentsize = elf__rd16(b + 58, obj->endian);
        shnum = elf__rd16(b + 60, obj->endian);
        obj->shstrndx = elf__rd16(b + 62, obj->endian);
    }

    err = parse_program_headers(obj, phoff, phentsize, phnum);
    if (err != ELF_OK) {
        return err;
    }

    err = parse_sections(obj, shoff, shentsize, shnum);
    if (err != ELF_OK) {
        return err;
    }

    if (shnum > 0) {
        err = resolve_section_names(obj);
        if (err != ELF_OK) {
            return err;
        }
    }

    err = parse_symbols(obj, &maps, &map_count);
    if (err != ELF_OK) {
        return err;
    }

    err = parse_relocations(obj, maps, map_count);
    for (i = 0; i < map_count; ++i) {
        free(maps[i].symbols);
    }
    free(maps);
    if (err != ELF_OK) {
        return err;
    }

    detect_special_sections(obj);
    obj->readonly = 1;
    obj->dirty = 0;
    return ELF_OK;
}

elf_err_t elf_open_memory(const void *buf, size_t size, elfobj_t **out) {
    elfobj_t *obj;
    elf_err_t err;

    if (buf == NULL || out == NULL) {
        return ELF_ERR_STATE;
    }

    obj = elf__alloc_obj();
    if (obj == NULL) {
        return ELF_ERR_OOM;
    }

    obj->image = (uint8_t *)malloc(size);
    if (obj->image == NULL) {
        elf_close(obj);
        return ELF_ERR_OOM;
    }
    memcpy(obj->image, buf, size);
    obj->image_size = size;
    obj->owns_image = 1;

    err = parse_object(obj);
    if (err != ELF_OK) {
        elf__set_err(obj, err, "failed to parse ELF object");
        elf_close(obj);
        return err;
    }

    *out = obj;
    return ELF_OK;
}

elf_err_t elf_open(const char *path, elfobj_t **out) {
    FILE *fp;
    uint8_t *buf;
    long end;
    size_t got;
    elf_err_t err;

    if (path == NULL || out == NULL) {
        return ELF_ERR_STATE;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return ELF_ERR_IO;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return ELF_ERR_IO;
    }
    end = ftell(fp);
    if (end < 0) {
        fclose(fp);
        return ELF_ERR_IO;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return ELF_ERR_IO;
    }

    buf = (uint8_t *)malloc((size_t)end);
    if (buf == NULL && end != 0) {
        fclose(fp);
        return ELF_ERR_OOM;
    }

    got = fread(buf, 1, (size_t)end, fp);
    fclose(fp);
    if (got != (size_t)end) {
        free(buf);
        return ELF_ERR_IO;
    }

    err = elf_open_memory(buf, (size_t)end, out);
    free(buf);
    return err;
}
