/*
 * ld_dynamic.c -- the dynamic side of the output: .dynsym, imports, PLT and GOT, .dynamic.
 */

#include "ld.h"

static int symbol_is_copied(const dyn_import_vec_t *imports, const elf_symbol_t *sym);

static int dynbuf_append(uint8_t **buf, size_t *len, size_t *cap, const void *src, size_t n) {
    uint8_t *next;
    size_t ncap;

    if (buf == NULL || len == NULL || cap == NULL) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (*len > SIZE_MAX - n) {
        return -1;
    }
    if (*len + n > *cap) {
        ncap = *cap == 0 ? 64 : *cap;
        while (ncap < *len + n) {
            if (ncap > SIZE_MAX / 2) {
                ncap = *len + n;
                break;
            }
            ncap *= 2;
        }
        next = (uint8_t *)realloc(*buf, ncap);
        if (next == NULL) {
            return -1;
        }
        *buf = next;
        *cap = ncap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    return 0;
}

int dynstr_append_cstr(uint8_t **buf, size_t *len, size_t *cap, const char *name, uint32_t *out_off) {
    size_t n;
    size_t off;

    if (buf == NULL || len == NULL || cap == NULL || out_off == NULL || name == NULL) {
        return -1;
    }
    off = *len;
    if (off > UINT32_MAX) {
        return -1;
    }
    n = strlen(name) + 1;
    if (dynbuf_append(buf, len, cap, name, n) != 0) {
        return -1;
    }
    *out_off = (uint32_t)off;
    return 0;
}

static int dynsym_should_export(const ld_ctx_t *ctx, const elfobj_t *out, const elf_symbol_t *sym) {
    uint8_t bind;
    uint8_t vis;
    uint16_t shndx;

    if (ctx == NULL || out == NULL || sym == NULL) {
        return 0;
    }
    if (elf_symbol_name(sym) == NULL || elf_symbol_name(sym)[0] == '\0') {
        return 0;
    }
    bind = elf_symbol_bind(sym);
    if (bind != STB_GLOBAL && bind != STB_WEAK) {
        return 0;
    }
    vis = elf_symbol_visibility(sym);
    if (vis != STV_DEFAULT && vis != STV_PROTECTED) {
        return 0;
    }
    shndx = elf_symbol_shndx(sym);
    if (elf_type(out) == ET_DYN) {
        return 1;
    }
    if (shndx == SHN_UNDEF) {
        return 1;
    }
    /* The executable's copy of a shared object's variable is the variable,
     * and the shared object has to be able to find it. */
    if (symbol_is_copied(&ctx->dyn_imports, sym)) {
        return 1;
    }
    /* What a shared object of the link refers to and the program has. */
    if (symset_contains(&ctx->dso_wants, elf_symbol_name(sym))) {
        return 1;
    }
    return ctx->export_dynamic ? 1 : 0;
}

static int dynamic_append_entry(uint8_t **buf, size_t *len, size_t *cap, elfobj_class_t cls,
                                elfobj_endian_t endian, int64_t tag, uint64_t value) {
    uint8_t entry[16];
    size_t entsz;

    if (cls == ELFOBJ_CLASS_64) {
        entsz = 16;
        write_u64_endian(entry + 0, endian, (uint64_t)tag);
        write_u64_endian(entry + 8, endian, value);
    } else {
        entsz = 8;
        write_u32_endian(entry + 0, endian, (uint32_t)tag);
        write_u32_endian(entry + 4, endian, (uint32_t)value);
    }
    return dynbuf_append(buf, len, cap, entry, entsz);
}

static uint32_t dynsym_name_off_at(const uint8_t *dynsym, size_t dynsym_len, size_t entsz,
                                   elfobj_endian_t endian, size_t index) {
    size_t off = index * entsz;
    if (dynsym == NULL || entsz == 0 || off > dynsym_len || dynsym_len - off < entsz) {
        return 0;
    }
    return read_u32_endian(dynsym + off, endian);
}

static uint8_t *build_sysv_hash_section(const uint8_t *dynsym, size_t dynsym_len,
                                        const uint8_t *dynstr, size_t dynstr_len,
                                        size_t entsz, elfobj_endian_t endian, size_t *out_sz) {
    size_t nsyms;
    size_t nbucket;
    size_t nchain;
    uint32_t *buckets = NULL;
    uint32_t *chains = NULL;
    uint8_t *buf = NULL;
    size_t i;
    size_t j;

    if (out_sz == NULL || entsz == 0 || (dynsym_len % entsz) != 0) {
        return NULL;
    }
    nsyms = dynsym_len / entsz;
    if (nsyms > SIZE_MAX / 8 - 1) {
        return NULL;
    }
    nbucket = nsyms > 1 ? nsyms - 1 : 1;
    nchain = nsyms;
    buckets = (uint32_t *)calloc(nbucket, sizeof(*buckets));
    chains = (uint32_t *)calloc(nchain, sizeof(*chains));
    if (buckets == NULL || chains == NULL) {
        free(buckets);
        free(chains);
        return NULL;
    }

    for (i = 1; i < nsyms; ++i) {
        uint32_t noff = dynsym_name_off_at(dynsym, dynsym_len, entsz, endian, i);
        const char *name = (noff < dynstr_len) ? (const char *)(dynstr + noff) : "";
        uint32_t h = elf_hash_sysv(name);
        size_t b = (size_t)(h % (uint32_t)nbucket);

        if (buckets[b] == 0) {
            buckets[b] = (uint32_t)i;
        } else {
            j = buckets[b];
            while (j < nchain && chains[j] != 0) {
                j = chains[j];
            }
            if (j < nchain) {
                chains[j] = (uint32_t)i;
            }
        }
    }

    *out_sz = (2 + nbucket + nchain) * 4;
    buf = (uint8_t *)malloc(*out_sz);
    if (buf == NULL) {
        free(buckets);
        free(chains);
        return NULL;
    }
    write_u32_endian(buf + 0, endian, (uint32_t)nbucket);
    write_u32_endian(buf + 4, endian, (uint32_t)nchain);
    for (i = 0; i < nbucket; ++i) {
        write_u32_endian(buf + 8 + (i * 4), endian, buckets[i]);
    }
    for (i = 0; i < nchain; ++i) {
        write_u32_endian(buf + 8 + (nbucket * 4) + (i * 4), endian, chains[i]);
    }
    free(buckets);
    free(chains);
    return buf;
}

static uint8_t *build_gnu_hash_section(const uint8_t *dynsym, size_t dynsym_len,
                                       const uint8_t *dynstr, size_t dynstr_len,
                                       size_t entsz, elfobj_class_t cls,
                                       elfobj_endian_t endian, size_t *out_sz) {
    size_t nsyms;
    uint32_t nbuckets;
    uint32_t symoffset = 1;
    uint32_t bloom_size = 1;
    uint32_t bloom_shift = 5;
    size_t chain_count;
    size_t word_sz;
    size_t i;
    uint8_t *buf = NULL;
    size_t off;

    if (out_sz == NULL || entsz == 0 || (dynsym_len % entsz) != 0) {
        return NULL;
    }
    nsyms = dynsym_len / entsz;
    nbuckets = (nsyms > 1) ? 1u : 1u;
    chain_count = (nsyms > symoffset) ? (nsyms - symoffset) : 0;
    word_sz = cls == ELFOBJ_CLASS_64 ? 8 : 4;
    *out_sz = 16 + (bloom_size * word_sz) + ((size_t)nbuckets * 4) + (chain_count * 4);
    buf = (uint8_t *)calloc(1, *out_sz);
    if (buf == NULL) {
        return NULL;
    }

    write_u32_endian(buf + 0, endian, nbuckets);
    write_u32_endian(buf + 4, endian, symoffset);
    write_u32_endian(buf + 8, endian, bloom_size);
    write_u32_endian(buf + 12, endian, bloom_shift);

    off = 16;
    if (chain_count > 0) {
        uint64_t bloom = 0;
        if (cls == ELFOBJ_CLASS_64) {
            write_u64_endian(buf + off, endian, 0);
        } else {
            write_u32_endian(buf + off, endian, 0);
        }
        off += word_sz;
        write_u32_endian(buf + off, endian, symoffset);
        off += 4;
        for (i = 0; i < chain_count; ++i) {
            size_t sym_index = symoffset + i;
            uint32_t noff = dynsym_name_off_at(dynsym, dynsym_len, entsz, endian, sym_index);
            const char *name = (noff < dynstr_len) ? (const char *)(dynstr + noff) : "";
            uint32_t h = elf_hash_gnu(name);
            uint32_t chain = h & ~1u;
            if (i + 1 == chain_count) {
                chain |= 1u;
            }
            if (cls == ELFOBJ_CLASS_64) {
                bloom |= (1ull << (h % 64));
                bloom |= (1ull << ((h >> bloom_shift) % 64));
            } else {
                bloom |= (1u << (h % 32));
                bloom |= (1u << ((h >> bloom_shift) % 32));
            }
            write_u32_endian(buf + off + (i * 4), endian, chain);
        }
        if (cls == ELFOBJ_CLASS_64) {
            write_u64_endian(buf + 16, endian, bloom);
        } else {
            write_u32_endian(buf + 16, endian, (uint32_t)bloom);
        }
    } else {
        if (cls == ELFOBJ_CLASS_64) {
            write_u64_endian(buf + off, endian, 0);
        } else {
            write_u32_endian(buf + off, endian, 0);
        }
        off += word_sz;
        write_u32_endian(buf + off, endian, 0);
    }
    return buf;
}

int is_runtime_import_symbol(const elf_symbol_t *sym) {
    uint8_t bind;
    uint8_t vis;

    if (sym == NULL || elf_symbol_name(sym) == NULL || elf_symbol_name(sym)[0] == '\0') {
        return 0;
    }
    if (elf_symbol_shndx(sym) != SHN_UNDEF) {
        return 0;
    }
    bind = elf_symbol_bind(sym);
    if (bind != STB_GLOBAL && bind != STB_WEAK) {
        return 0;
    }
    vis = elf_symbol_visibility(sym);
    if (vis != STV_DEFAULT && vis != STV_PROTECTED) {
        return 0;
    }
    return 1;
}

int reloc_is_x64_plt_ref(uint32_t type) {
    return type == R_X86_64_PLT32;
}

int reloc_is_x64_got_ref(uint32_t type) {
    switch (type) {
    case R_X86_64_GOT32:
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPC32:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX:
        return 1;
    default:
        return 0;
    }
}

int reloc_is_x64_tls_gd_ref(uint32_t type) {
    return type == R_X86_64_TLSGD;
}

int reloc_is_x64_tls_ie_ref(uint32_t type) {
    return type == R_X86_64_GOTTPOFF;
}

int reloc_is_x64_runtime_data_ref(uint32_t type) {
    return type == R_X86_64_64;
}

/*
 * A reference that is a call, and so can be sent through the PLT when
 * what it names turns out to be in a shared object.  R_386_PLT32 says so
 * outright.  R_386_PC32 is what a compiler not told to make
 * position-independent code writes for every call; on i386 nothing but a
 * branch is pc-relative, so against an import it is one too.
 */
int reloc_is_i386_plt_ref(uint32_t type) {
    return type == R_386_PLT32 || type == R_386_PC32;
}

int reloc_is_i386_got_ref(uint32_t type) {
    switch (type) {
    case R_386_GOT32:
    case R_386_GOT32X:
        return 1;
    default:
        return 0;
    }
}

int reloc_is_i386_tls_gd_ref(uint32_t type) {
    return type == R_386_TLS_GD;
}

int reloc_is_i386_tls_ie_ref(uint32_t type) {
    return type == R_386_TLS_IE || type == R_386_TLS_GOTIE;
}

int reloc_is_i386_runtime_data_ref(uint32_t type) {
    return type == R_386_32;
}

static int symbol_needs_runtime_relative_reloc(const elf_symbol_t *sym) {
    uint16_t shndx;

    if (sym == NULL) {
        return 0;
    }
    shndx = elf_symbol_shndx(sym);
    if (shndx == SHN_UNDEF || shndx == SHN_ABS || shndx == SHN_COMMON || shndx >= 0xff00) {
        return 0;
    }
    return 1;
}

static int resolve_runtime_relative_addend(elfobj_t *obj, const elf_symbol_t *sym, int64_t addend,
                                           uint64_t *out_addend) {
    uint64_t sym_addr;

    if (out_addend == NULL) {
        return -1;
    }
    if (resolve_symbol_addr(obj, sym, 1, &sym_addr, NULL) != 0) {
        return -1;
    }
    if (addend >= 0) {
        uint64_t uadd = (uint64_t)addend;
        if (sym_addr > UINT64_MAX - uadd) {
            return -1;
        }
        *out_addend = sym_addr + uadd;
    } else {
        uint64_t neg = (uint64_t)(-(addend + 1)) + 1u;
        if (sym_addr < neg) {
            return -1;
        }
        *out_addend = sym_addr - neg;
    }
    return 0;
}

/* Whether an import is one whose address is its PLT entry. */
static int import_is_canonical(const dyn_import_vec_t *imports, const elf_symbol_t *sym) {
    int idx = sym != NULL ? dyn_import_find(imports, elf_symbol_name(sym)) : -1;

    return idx >= 0 && imports->items[idx].canonical;
}

static size_t count_runtime_data_import_relocs_x64(elfobj_t *out, const dyn_import_vec_t *imports) {
    size_t i;
    size_t n = 0;

    if (out == NULL) {
        return 0;
    }
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);
        size_t ri;
        size_t rc;

        if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
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
            if (reloc_is_x64_runtime_data_ref(elf_reloc_type(rel)) &&
                ((is_runtime_import_symbol(sym) && !import_is_canonical(imports, sym)) ||
                 (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym)))) {
                n++;
            }
        }
    }
    return n;
}

static size_t count_runtime_data_import_relocs_i386(elfobj_t *out, const dyn_import_vec_t *imports) {
    size_t i;
    size_t n = 0;

    if (out == NULL) {
        return 0;
    }
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);
        size_t ri;
        size_t rc;

        if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
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
            if (reloc_is_i386_runtime_data_ref(elf_reloc_type(rel)) &&
                ((is_runtime_import_symbol(sym) && !import_is_canonical(imports, sym)) ||
                 (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym)))) {
                n++;
            }
        }
    }
    return n;
}

/* The definition of `name` among the shared objects of the link, the first
 * there is.  0, or -1 if none defines it. */
static int dso_find_definition(const ld_ctx_t *ctx, const char *name, dso_def_t *def) {
    size_t d, i;

    for (d = 0; ctx != NULL && name != NULL && d < ctx->dso_inputs.count; ++d) {
        elfobj_t *obj = NULL;
        int found = 0;

        if (elf_open(ctx->dso_inputs.items[d], &obj) != ELF_OK) {
            continue;
        }
        for (i = 0; !found && i < elf_symbol_count(obj); ++i) {
            const elf_symbol_t *sym = elf_symbol_at(obj, i);
            const char *sname = sym != NULL ? elf_symbol_name(sym) : NULL;

            if (sname != NULL && elf_symbol_shndx(sym) != SHN_UNDEF && strcmp(sname, name) == 0 &&
                (elf_symbol_bind(sym) == STB_GLOBAL || elf_symbol_bind(sym) == STB_WEAK)) {
                def->type = elf_symbol_type(sym);
                def->size = elf_symbol_size(sym);
                def->value = elf_symbol_value(sym);
                def->shndx = elf_symbol_shndx(sym);
                def->dso = d;
                found = 1;
            }
        }
        elf_close(obj);
        if (found) {
            return 0;
        }
    }
    return -1;
}

/*
 * What the shared objects of the link define `name` as (STT_FUNC,
 * STT_OBJECT, ...); -1 if none of them defines it.
 */
static int dso_definition_type(const ld_ctx_t *ctx, const char *name) {
    dso_def_t def;

    return dso_find_definition(ctx, name, &def) == 0 ? def.type : -1;
}

/*
 * What a program that is not position-independent does about the things
 * of shared objects it refers to directly.
 *
 * Such code has the address of what it names built into its instructions,
 * and its instructions cannot be changed when it is loaded.  So what it
 * names has to be somewhere the linker knows now:
 *
 *   - a variable gets a copy in the executable (.dynbss), which becomes
 *     the variable for everyone: the executable defines the symbol, and
 *     an R_*_COPY relocation has the dynamic linker fill the copy from
 *     the shared object's initial value before anything runs.  The other
 *     names the shared object has for the same variable (environ and
 *     __environ) are defined at the copy too, or the library would go on
 *     using its own.  This is done here, before imports are collected:
 *     once defined, the symbol is not an import.
 *
 *   - a function whose address is taken has its PLT entry for an
 *     address.  The symbol stays undefined, with the PLT entry as its
 *     value, which tells the dynamic linker to give that same address to
 *     every other module that asks.  The import is marked `canonical`
 *     when imports are collected.
 *
 * A shared object, and position-independent code anywhere, needs neither:
 * it reaches everything through the GOT.
 */
int reloc_is_direct_ref(uint16_t machine, uint32_t type, int data) {
    if (machine == EM_386) {
        return type == R_386_32;
    }
    if (machine == EM_X86_64) {
        return type == R_X86_64_64 || type == R_X86_64_32 || type == R_X86_64_32S ||
               (data && type == R_X86_64_PC32);
    }
    return 0;
}

static int symvec_push(elf_symbol_t ***items, size_t *count, size_t *cap, elf_symbol_t *sym) {
    if (*count == *cap) {
        size_t ncap = *cap ? *cap * 2 : 8;
        elf_symbol_t **n = (elf_symbol_t **)realloc(*items, ncap * sizeof(*n));

        if (n == NULL) {
            return -1;
        }
        *items = n;
        *cap = ncap;
    }
    (*items)[(*count)++] = sym;
    return 0;
}

static int plan_copy_relocs(const ld_ctx_t *ctx, elfobj_t *out, dyn_import_vec_t *imports) {
    uint16_t machine = elf_machine(out);
    size_t si, ri, k;

    if (elf_type(out) != ET_EXEC || ctx->dso_inputs.count == 0) {
        return 0;
    }
    for (si = 0; si < elf_section_count(out); ++si) {
        elf_section_t *sec = elf_section_get(out, si);

        if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
            continue;
        }
        for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            elf_symbol_t *sym = rel != NULL ? (elf_symbol_t *)elf_reloc_symbol(rel) : NULL;
            elf_section_t *dynbss;
            elfobj_t *dso = NULL;
            uint64_t align, off;
            dso_def_t def;

            if (sym == NULL || !is_runtime_import_symbol(sym) ||
                !reloc_is_direct_ref(machine, elf_reloc_type(rel), 1) ||
                dso_find_definition(ctx, elf_symbol_name(sym), &def) != 0) {
                continue;
            }
            if (def.type == STT_TLS) {
                fprintf(stderr, "ld: %s is thread-local in a shared object and is referred to directly: "
                                "not supported; compile with -fPIC\n", elf_symbol_name(sym));
                return -1;
            }
            if (def.type != STT_OBJECT) {
                continue;
            }
            dynbss = elf_find_section(out, ".dynbss");
            if (dynbss == NULL) {
                dynbss = elf_add_section(out, ".dynbss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
                if (dynbss == NULL) {
                    return -1;
                }
            }
            /* As aligned as anything of its size can need to be. */
            for (align = 1; align < 16 && align * 2 <= def.size; align *= 2) {
            }
            off = (elf_section_size(dynbss) + align - 1) & ~(align - 1);
            if (elf_section_align(dynbss) < align && elf_section_set_align(dynbss, align) != ELF_OK) {
                return -1;
            }
            if (set_section_zero_data(dynbss, (size_t)(off + (def.size != 0 ? def.size : 1))) != 0 ||
                elf_symbol_define(sym, dynbss, off) != ELF_OK || elf_symbol_set_type(sym, STT_OBJECT) != ELF_OK ||
                elf_symbol_set_size(sym, def.size) != ELF_OK ||
                symvec_push(&imports->copies, &imports->copy_count, &imports->copy_cap, sym) != 0) {
                return -1;
            }
            /* Its other names. */
            if (elf_open(ctx->dso_inputs.items[def.dso], &dso) != ELF_OK) {
                continue;
            }
            for (k = 0; k < elf_symbol_count(dso); ++k) {
                const elf_symbol_t *other = elf_symbol_at(dso, k);
                const char *oname = other != NULL ? elf_symbol_name(other) : NULL;
                elf_symbol_t *alias;

                if (oname == NULL || oname[0] == '\0' || strcmp(oname, elf_symbol_name(sym)) == 0 ||
                    elf_symbol_shndx(other) != def.shndx || elf_symbol_value(other) != def.value ||
                    elf_symbol_type(other) != STT_OBJECT ||
                    (elf_symbol_bind(other) != STB_GLOBAL && elf_symbol_bind(other) != STB_WEAK)) {
                    continue;
                }
                alias = elf_find_symbol(out, oname);
                if (alias != NULL && elf_symbol_shndx(alias) != SHN_UNDEF) {
                    continue;   /* the program has one of its own by that name */
                }
                if (alias == NULL) {
                    alias = elf_add_symbol(out, oname, 0, 0, elf_symbol_bind(other), STT_OBJECT);
                }
                if (alias == NULL || elf_symbol_define(alias, dynbss, off) != ELF_OK ||
                    elf_symbol_set_type(alias, STT_OBJECT) != ELF_OK || elf_symbol_set_size(alias, def.size) != ELF_OK ||
                    symvec_push(&imports->copy_aliases, &imports->alias_count, &imports->alias_cap, alias) != 0) {
                    elf_close(dso);
                    return -1;
                }
            }
            elf_close(dso);
        }
    }
    return 0;
}

/* Whether this reference, from an executable, takes the address of a
 * function that is in a shared object: the address is its PLT entry. */
static int import_address_is_plt(const ld_ctx_t *ctx, const elfobj_t *out, uint32_t type, const elf_symbol_t *sym) {
    int what;

    if (elf_type(out) != ET_EXEC || !reloc_is_direct_ref(elf_machine(out), type, 0) ||
        !is_runtime_import_symbol(sym)) {
        return 0;
    }
    what = dso_definition_type(ctx, elf_symbol_name(sym));
    return what == STT_FUNC || what == STT_GNU_IFUNC;
}

/* Whether the executable has a copy of this symbol's data. */
static int symbol_is_copied(const dyn_import_vec_t *imports, const elf_symbol_t *sym) {
    size_t i;

    for (i = 0; i < imports->copy_count; ++i) {
        if (imports->copies[i] == sym) {
            return 1;
        }
    }
    for (i = 0; i < imports->alias_count; ++i) {
        if (imports->copy_aliases[i] == sym) {
            return 1;
        }
    }
    return 0;
}

/* The same, where a symbol nothing defines is one of no type. */
static int dso_import_type(const ld_ctx_t *ctx, const char *name) {
    int type = dso_definition_type(ctx, name);

    return type >= 0 ? type : STT_NOTYPE;
}

/*
 * A weak reference that nothing in the link defines, neither an input nor
 * a shared object, is zero, and in an executable it is zero now: the
 * program asks "is this here?" and the answer does not wait for run time.
 * Left as an import it became a relocation for the dynamic linker to
 * apply to the instruction that asks, in memory that cannot be written.
 */
int settle_undefined_weak(const ld_ctx_t *ctx, elfobj_t *out) {
    size_t i;

    if (elf_type(out) != ET_EXEC || ctx->dso_inputs.count == 0) {
        return 0;
    }
    for (i = 0; i < elf_symbol_count(out); ++i) {
        elf_symbol_t *sym = elf_symbol_at(out, i);
        const char *name = sym != NULL ? elf_symbol_name(sym) : NULL;

        if (name == NULL || name[0] == '\0' || elf_symbol_shndx(sym) != SHN_UNDEF ||
            elf_symbol_bind(sym) != STB_WEAK || dso_definition_type(ctx, name) >= 0) {
            continue;
        }
        if (elf_symbol_set_value(sym, 0) != ELF_OK || elf_symbol_set_shndx(sym, SHN_ABS) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

static int collect_dynamic_imports_x64(const ld_ctx_t *ctx, elfobj_t *out, dyn_import_vec_t *imports) {
    size_t i;

    if (out == NULL || imports == NULL) {
        return -1;
    }
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);
        size_t ri;
        size_t rc;

        if (sec == NULL) {
            continue;
        }
        rc = elf_section_reloc_count(sec);
        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym;
            dyn_import_t *imp;
            uint32_t type;
            int plt_ref;
            int canonical;

            if (rel == NULL) {
                continue;
            }
            sym = elf_reloc_symbol(rel);
            if (!is_runtime_import_symbol(sym)) {
                continue;
            }
            type = elf_reloc_type(rel);
            plt_ref = reloc_is_x64_plt_ref(type);
            if (!plt_ref && type == R_X86_64_PC32 &&
                (elf_symbol_type(sym) == STT_FUNC || elf_symbol_type(sym) == STT_NOTYPE)) {
                /*
                 * A pc-relative reference to something in a shared
                 * object.  A reference does not say what it refers to
                 * (the symbol is undefined, of no type), and on x86-64
                 * data is addressed this way as much as functions are
                 * called this way; the shared object says which.  A call
                 * goes through the PLT.  Data would need a copy of it in
                 * the executable, which is not made here, and to send
                 * it through the PLT is to read the PLT's instructions
                 * as the variable.
                 */
                int what = elf_symbol_type(sym) == STT_FUNC ? STT_FUNC : dso_import_type(ctx, elf_symbol_name(sym));

                if (what == STT_OBJECT || what == STT_TLS) {
                    fprintf(stderr,
                            "ld: %s is data in a shared object and is referred to pc-relatively from %s: "
                            "that needs a copy relocation, which is not supported; compile with -fPIC\n",
                            elf_symbol_name(sym), elf_section_name(sec) != NULL ? elf_section_name(sec) : "?");
                    return -1;
                }
                plt_ref = 1;
            }
            canonical = !plt_ref && import_address_is_plt(ctx, out, type, sym);
            plt_ref |= canonical;
            if (!plt_ref && !reloc_is_x64_got_ref(type) &&
                !reloc_is_x64_tls_gd_ref(type) && !reloc_is_x64_tls_ie_ref(type) &&
                !reloc_is_x64_runtime_data_ref(type)) {
                continue;
            }
            imp = dyn_import_get_or_add(imports, elf_symbol_name(sym));
            if (imp == NULL) {
                return -1;
            }
            if (plt_ref) {
                imp->need_plt = 1;
            }
            if (canonical) {
                imp->canonical = 1;
            }
            if (reloc_is_x64_got_ref(type)) {
                imp->need_got = 1;
            }
            if (reloc_is_x64_tls_gd_ref(type)) {
                imp->need_tls_gd = 1;
            }
            if (reloc_is_x64_tls_ie_ref(type)) {
                imp->need_tls_ie = 1;
            }
        }
    }
    return 0;
}

static int collect_dynamic_imports_i386(const ld_ctx_t *ctx, elfobj_t *out, dyn_import_vec_t *imports) {
    size_t i;

    if (out == NULL || imports == NULL) {
        return -1;
    }
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);
        size_t ri;
        size_t rc;

        if (sec == NULL) {
            continue;
        }
        rc = elf_section_reloc_count(sec);
        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym;
            dyn_import_t *imp;
            uint32_t type;

            if (rel == NULL) {
                continue;
            }
            sym = elf_reloc_symbol(rel);
            if (!is_runtime_import_symbol(sym)) {
                continue;
            }
            type = elf_reloc_type(rel);
            if (!reloc_is_i386_plt_ref(type) && !reloc_is_i386_got_ref(type) &&
                !reloc_is_i386_tls_gd_ref(type) && !reloc_is_i386_tls_ie_ref(type) &&
                !reloc_is_i386_runtime_data_ref(type)) {
                continue;
            }
            imp = dyn_import_get_or_add(imports, elf_symbol_name(sym));
            if (imp == NULL) {
                return -1;
            }
            if (reloc_is_i386_plt_ref(type)) {
                imp->need_plt = 1;
            }
            if (import_address_is_plt(ctx, out, type, sym)) {
                imp->need_plt = 1;
                imp->canonical = 1;
            }
            if (reloc_is_i386_got_ref(type)) {
                imp->need_got = 1;
            }
            if (reloc_is_i386_tls_gd_ref(type)) {
                imp->need_tls_gd = 1;
            }
            if (reloc_is_i386_tls_ie_ref(type)) {
                imp->need_tls_ie = 1;
            }
        }
    }
    return 0;
}

int set_section_zero_data(elf_section_t *sec, size_t sz) {
    uint8_t *buf = NULL;
    int rc = -1;

    if (sec == NULL) {
        return -1;
    }
    if (sz != 0) {
        buf = (uint8_t *)calloc(1, sz);
        if (buf == NULL) {
            return -1;
        }
    }
    if (elf_section_set_data(sec, buf, sz) == ELF_OK) {
        rc = 0;
    }
    free(buf);
    return rc;
}

static int ensure_dynamic_import_sections_x64(elfobj_t *out, const dyn_import_vec_t *imports, size_t extra_dyn_count,
                                              size_t extra_got) {
    size_t i;
    size_t plt_count = 0;
    size_t got_count = 0;
    size_t dyn_count = 0;
    elf_section_t *sec;

    if (out == NULL || imports == NULL) {
        return -1;
    }
    for (i = 0; i < imports->count; ++i) {
        if (imports->items[i].need_plt) {
            plt_count++;
        }
        if (imports->items[i].need_got) {
            got_count++;
        }
        if (imports->items[i].need_tls_ie) {
            got_count++;
        }
        if (imports->items[i].need_tls_gd) {
            got_count += 2;
        }
    }
    dyn_count = got_count + extra_dyn_count;
    got_count += extra_got;     /* the output's own slots, after the imports' */

    if (plt_count != 0) {
        sec = elf_find_section(out, ".plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".plt", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 16) != ELF_OK || set_section_zero_data(sec, 16 * (1 + plt_count)) != 0) {
            return -1;
        }

        sec = elf_find_section(out, ".got.plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".got.plt", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 8) != ELF_OK || set_section_zero_data(sec, 8 * (3 + plt_count)) != 0) {
            return -1;
        }

        sec = elf_find_section(out, ".rela.plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".rela.plt", SHT_RELA, SHF_ALLOC);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 8) != ELF_OK || set_section_zero_data(sec, 24 * plt_count) != 0) {
            return -1;
        }
    }

    if (got_count != 0) {
        sec = elf_find_section(out, ".got");
        if (sec == NULL) {
            sec = elf_add_section(out, ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 8) != ELF_OK || set_section_zero_data(sec, 8 * got_count) != 0) {
            return -1;
        }

    }

    if (dyn_count != 0) {
        sec = elf_find_section(out, ".rela.dyn");
        if (sec == NULL) {
            sec = elf_add_section(out, ".rela.dyn", SHT_RELA, SHF_ALLOC);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 8) != ELF_OK || set_section_zero_data(sec, 24 * dyn_count) != 0) {
            return -1;
        }
    }
    return 0;
}

static int ensure_dynamic_import_sections_i386(elfobj_t *out, const dyn_import_vec_t *imports, size_t extra_dyn_count,
                                               size_t extra_got) {
    size_t i;
    size_t plt_count = 0;
    size_t got_count = 0;
    size_t dyn_count = 0;
    elf_section_t *sec;

    if (out == NULL || imports == NULL) {
        return -1;
    }
    for (i = 0; i < imports->count; ++i) {
        if (imports->items[i].need_plt) {
            plt_count++;
        }
        if (imports->items[i].need_got) {
            got_count++;
        }
        if (imports->items[i].need_tls_ie) {
            got_count++;
        }
        if (imports->items[i].need_tls_gd) {
            got_count += 2;
        }
    }
    dyn_count = got_count + extra_dyn_count;
    got_count += extra_got;     /* the output's own slots, after the imports' */

    if (plt_count != 0) {
        sec = elf_find_section(out, ".plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".plt", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 16) != ELF_OK || set_section_zero_data(sec, 16 * (1 + plt_count)) != 0) {
            return -1;
        }

        sec = elf_find_section(out, ".got.plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".got.plt", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || set_section_zero_data(sec, 4 * (3 + plt_count)) != 0) {
            return -1;
        }

        sec = elf_find_section(out, ".rel.plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".rel.plt", SHT_REL, SHF_ALLOC);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || set_section_zero_data(sec, 8 * plt_count) != 0) {
            return -1;
        }
    }

    if (got_count != 0) {
        sec = elf_find_section(out, ".got");
        if (sec == NULL) {
            sec = elf_add_section(out, ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || set_section_zero_data(sec, 4 * got_count) != 0) {
            return -1;
        }

    }

    if (dyn_count != 0) {
        sec = elf_find_section(out, ".rel.dyn");
        if (sec == NULL) {
            sec = elf_add_section(out, ".rel.dyn", SHT_REL, SHF_ALLOC);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || set_section_zero_data(sec, 8 * dyn_count) != 0) {
            return -1;
        }
    }
    return 0;
}

int plan_dynamic_imports(ld_ctx_t *ctx, elfobj_t *out) {
    size_t i;
    size_t plt_slot = 0;
    size_t got_slot = 0;
    size_t extra_dyn_relocs = 0;
    static int trace_imports_env = -1;

    if (trace_imports_env < 0) {
        const char *v = getenv("LD_DEBUG_IMPORTS");
        trace_imports_env = (v != NULL && v[0] != '\0') ? 1 : 0;
    }

    if (ctx == NULL || out == NULL) {
        return -1;
    }
    if ((ctx->mode != 64 && ctx->mode != 32) || (elf_type(out) != ET_DYN && ctx->dso_inputs.count == 0)) {
        return collect_local_got(ctx, out);
    }
    dyn_import_vec_free(&ctx->dyn_imports);
    if (plan_copy_relocs(ctx, out, &ctx->dyn_imports) != 0 || collect_local_got(ctx, out) != 0) {
        return -1;
    }
    if (ctx->mode == 64) {
        if (collect_dynamic_imports_x64(ctx, out, &ctx->dyn_imports) != 0) {
            return -1;
        }
        extra_dyn_relocs = count_runtime_data_import_relocs_x64(out, &ctx->dyn_imports);
    } else {
        if (collect_dynamic_imports_i386(ctx, out, &ctx->dyn_imports) != 0) {
            return -1;
        }
        extra_dyn_relocs = count_runtime_data_import_relocs_i386(out, &ctx->dyn_imports);
    }
    extra_dyn_relocs += ctx->dyn_imports.copy_count;
    for (i = 0; i < ctx->dyn_imports.count; ++i) {
        if (ctx->dyn_imports.items[i].need_plt) {
            ctx->dyn_imports.items[i].plt_slot = plt_slot++;
        }
        if (ctx->dyn_imports.items[i].need_got) {
            ctx->dyn_imports.items[i].got_slot = got_slot++;
        }
        if (ctx->dyn_imports.items[i].need_tls_ie) {
            ctx->dyn_imports.items[i].tls_ie_slot = got_slot++;
        }
        if (ctx->dyn_imports.items[i].need_tls_gd) {
            ctx->dyn_imports.items[i].tls_gd_slot = got_slot;
            got_slot += 2;
        }
    }
    if (trace_imports_env) {
        fprintf(stderr, "ld: import-plan: mode=%d imports=%zu extra-dyn=%zu plt=%zu got=%zu\n",
                ctx->mode, ctx->dyn_imports.count, extra_dyn_relocs, plt_slot, got_slot);
        for (i = 0; i < ctx->dyn_imports.count; ++i) {
            const dyn_import_t *imp = &ctx->dyn_imports.items[i];
            fprintf(stderr,
                    "ld: import-plan: %s plt=%d got=%d tls_gd=%d tls_ie=%d slots(plt=%zu got=%zu gd=%zu ie=%zu)\n",
                    imp->name != NULL ? imp->name : "<null>",
                    imp->need_plt, imp->need_got, imp->need_tls_gd, imp->need_tls_ie,
                    imp->plt_slot, imp->got_slot, imp->tls_gd_slot, imp->tls_ie_slot);
        }
    }
    /* The output's own slots come after the imports', and their
     * relocations after every other in .rel[a].dyn. */
    ctx->local_got_base = got_slot;
    extra_dyn_relocs += ctx->local_got_relative;
    if (ctx->mode == 64) {
        if (ensure_dynamic_import_sections_x64(out, &ctx->dyn_imports, extra_dyn_relocs, ctx->local_got_count) != 0) {
            return -1;
        }
    } else {
        if (ensure_dynamic_import_sections_i386(out, &ctx->dyn_imports, extra_dyn_relocs, ctx->local_got_count) != 0) {
            return -1;
        }
    }
    ctx->local_got_owned = ctx->local_got_count != 0;
    return 0;
}

static int dynsym_index_by_name(const elfobj_t *out, const char *name, uint32_t *out_index) {
    const elf_section_t *dynsym;
    const elf_section_t *dynstr;
    const uint8_t *sym_data;
    const uint8_t *str_data;
    size_t sym_sz = 0;
    size_t str_sz = 0;
    size_t entsz;
    size_t i;
    uint32_t nsyms;

    if (out == NULL || name == NULL || out_index == NULL) {
        return -1;
    }
    dynsym = elf_find_section((elfobj_t *)out, ".dynsym");
    dynstr = elf_find_section((elfobj_t *)out, ".dynstr");
    if (dynsym == NULL || dynstr == NULL) {
        return -1;
    }
    sym_data = (const uint8_t *)elf_section_data(dynsym, &sym_sz);
    str_data = (const uint8_t *)elf_section_data(dynstr, &str_sz);
    entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 24 : 16;
    if (sym_data == NULL || str_data == NULL || sym_sz < entsz || (sym_sz % entsz) != 0) {
        return -1;
    }
    nsyms = (uint32_t)(sym_sz / entsz);
    for (i = 1; i < nsyms; ++i) {
        uint32_t noff = read_u32_endian(sym_data + (i * entsz), elf_endian(out));
        const char *nm;
        if (noff >= str_sz) {
            continue;
        }
        nm = (const char *)(str_data + noff);
        if (strcmp(nm, name) == 0) {
            *out_index = (uint32_t)i;
            return 0;
        }
    }
    return -1;
}

int finalize_dynamic_imports_x64(elfobj_t *out, const dyn_import_vec_t *imports) {
    elf_section_t *plt;
    elf_section_t *gotplt;
    elf_section_t *got;
    elf_section_t *rela_plt;
    elf_section_t *rela_dyn;
    const elf_section_t *dynamic;
    uint8_t *plt_buf = NULL;
    uint8_t *gotplt_buf = NULL;
    uint8_t *got_buf = NULL;
    uint8_t *rela_plt_buf = NULL;
    uint8_t *rela_dyn_buf = NULL;
    size_t plt_sz = 0;
    size_t gotplt_sz = 0;
    size_t got_sz = 0;
    size_t rela_plt_sz = 0;
    size_t rela_dyn_sz = 0;
    size_t rela_dyn_base_count = 0;
    size_t runtime_extra_count = 0;
    size_t required_rela_dyn_sz = 0;
    size_t need_plt_count = 0;
    size_t i;
    uint64_t plt_addr;
    uint64_t gotplt_addr;
    uint64_t got_addr = 0;
    uint64_t dynamic_addr = 0;
    elfobj_endian_t e;

    if (out == NULL || imports == NULL) {
        return 0;
    }
    for (i = 0; i < imports->count; ++i) {
        if (imports->items[i].need_plt) {
            need_plt_count++;
        }
        if (imports->items[i].need_got) {
            rela_dyn_base_count++;
        }
        if (imports->items[i].need_tls_ie) {
            rela_dyn_base_count++;
        }
        if (imports->items[i].need_tls_gd) {
            rela_dyn_base_count += 2;
        }
    }
    runtime_extra_count = count_runtime_data_import_relocs_x64(out, imports) + imports->copy_count;
    required_rela_dyn_sz = (rela_dyn_base_count + runtime_extra_count) * 24;
    plt = elf_find_section(out, ".plt");
    gotplt = elf_find_section(out, ".got.plt");
    got = elf_find_section(out, ".got");
    rela_plt = elf_find_section(out, ".rela.plt");
    rela_dyn = elf_find_section(out, ".rela.dyn");
    dynamic = elf_find_section(out, ".dynamic");
    if (need_plt_count != 0 && (plt == NULL || gotplt == NULL)) {
        return -1;
    }

    plt_addr = plt != NULL ? elf_section_addr(plt) : 0;
    gotplt_addr = gotplt != NULL ? elf_section_addr(gotplt) : 0;
    if (got != NULL) {
        got_addr = elf_section_addr(got);
    }
    if (dynamic != NULL) {
        dynamic_addr = elf_section_addr(dynamic);
    }
    e = elf_endian(out);

    plt_sz = plt != NULL ? elf_section_size(plt) : 0;
    gotplt_sz = gotplt != NULL ? elf_section_size(gotplt) : 0;
    got_sz = got != NULL ? elf_section_size(got) : 0;
    rela_plt_sz = rela_plt != NULL ? elf_section_size(rela_plt) : 0;
    rela_dyn_sz = rela_dyn != NULL ? elf_section_size(rela_dyn) : 0;
    if (rela_dyn_sz < required_rela_dyn_sz) {
        rela_dyn_sz = required_rela_dyn_sz;
    }

    if (plt_sz != 0) {
        plt_buf = (uint8_t *)calloc(1, plt_sz);
        if (plt_buf == NULL) {
            return -1;
        }
    }
    if (gotplt_sz != 0) {
        gotplt_buf = (uint8_t *)calloc(1, gotplt_sz);
        if (gotplt_buf == NULL) {
            free(plt_buf);
            return -1;
        }
    }
    if (got_sz != 0) {
        got_buf = (uint8_t *)calloc(1, got_sz);
        if (got_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            return -1;
        }
    }
    if (rela_plt_sz != 0) {
        rela_plt_buf = (uint8_t *)calloc(1, rela_plt_sz);
        if (rela_plt_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            free(got_buf);
            return -1;
        }
    }
    if (rela_dyn_sz != 0) {
        rela_dyn_buf = (uint8_t *)calloc(1, rela_dyn_sz);
        if (rela_dyn_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            free(got_buf);
            free(rela_plt_buf);
            return -1;
        }
    }

    if (plt_buf != NULL && plt_sz >= 16) {
        int32_t disp;
        /* PLT0: pushq GOT+8(%rip); jmp *GOT+16(%rip); nopl 0(%rax) */
        plt_buf[0] = 0xff;
        plt_buf[1] = 0x35;
        disp = (int32_t)((int64_t)(gotplt_addr + 8) - (int64_t)(plt_addr + 6));
        write_u32_endian(plt_buf + 2, e, (uint32_t)disp);
        plt_buf[6] = 0xff;
        plt_buf[7] = 0x25;
        disp = (int32_t)((int64_t)(gotplt_addr + 16) - (int64_t)(plt_addr + 12));
        write_u32_endian(plt_buf + 8, e, (uint32_t)disp);
        plt_buf[12] = 0x0f;
        plt_buf[13] = 0x1f;
        plt_buf[14] = 0x40;
        plt_buf[15] = 0x00;
    }
    if (gotplt_buf != NULL && gotplt_sz >= 24) {
        write_u64_endian(gotplt_buf + 0, e, dynamic_addr);
        write_u64_endian(gotplt_buf + 8, e, 0);
        write_u64_endian(gotplt_buf + 16, e, 0);
    }

    for (i = 0; i < imports->count; ++i) {
        const dyn_import_t *imp = &imports->items[i];
        uint32_t dynidx = 0;
        if (dynsym_index_by_name(out, imp->name, &dynidx) != 0) {
            /* Its PLT entry and GOT slot would be left zero, to be
             * jumped through at run time. */
            fprintf(stderr, "ld: %s is imported and has no entry in the dynamic symbol table\n",
                    imp->name != NULL ? imp->name : "?");
            goto fail_import;
        }
        if (imp->need_plt) {
            size_t ent = imp->plt_slot;
            uint64_t ent_addr = plt_addr + 16 + (ent * 16);
            uint64_t slot_addr = gotplt_addr + 24 + (ent * 8);
            size_t poff = 16 + (ent * 16);
            size_t roff = ent * 24;
            int32_t disp;

            if (poff + 16 <= plt_sz) {
                plt_buf[poff + 0] = 0xff;
                plt_buf[poff + 1] = 0x25;
                disp = (int32_t)((int64_t)slot_addr - (int64_t)(ent_addr + 6));
                write_u32_endian(plt_buf + poff + 2, e, (uint32_t)disp);
                plt_buf[poff + 6] = 0x68;
                write_u32_endian(plt_buf + poff + 7, e, (uint32_t)ent);
                plt_buf[poff + 11] = 0xe9;
                disp = (int32_t)((int64_t)plt_addr - (int64_t)(ent_addr + 16));
                write_u32_endian(plt_buf + poff + 12, e, (uint32_t)disp);
            }
            if ((24 + ((ent + 1) * 8)) <= gotplt_sz) {
                write_u64_endian(gotplt_buf + 24 + (ent * 8), e, ent_addr + 6);
            }
            if (rela_plt_buf != NULL && roff + 24 <= rela_plt_sz) {
                write_u64_endian(rela_plt_buf + roff + 0, e, slot_addr);
                write_u64_endian(rela_plt_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_JUMP_SLOT);
                write_u64_endian(rela_plt_buf + roff + 16, e, 0);
            }
        }
        if (imp->need_got && got != NULL) {
            size_t ent = imp->got_slot;
            size_t roff = ent * 24;
            uint64_t slot_addr = got_addr + (ent * 8);

            if (rela_dyn_buf != NULL && roff + 24 <= rela_dyn_sz) {
                write_u64_endian(rela_dyn_buf + roff + 0, e, slot_addr);
                write_u64_endian(rela_dyn_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_GLOB_DAT);
                write_u64_endian(rela_dyn_buf + roff + 16, e, 0);
            }
        }
        if (imp->need_tls_ie && got != NULL) {
            size_t ent = imp->tls_ie_slot;
            size_t roff = ent * 24;
            uint64_t slot_addr = got_addr + (ent * 8);

            if (rela_dyn_buf != NULL && roff + 24 <= rela_dyn_sz) {
                write_u64_endian(rela_dyn_buf + roff + 0, e, slot_addr);
                write_u64_endian(rela_dyn_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_TPOFF64);
                write_u64_endian(rela_dyn_buf + roff + 16, e, 0);
            }
        }
        if (imp->need_tls_gd && got != NULL) {
            size_t ent = imp->tls_gd_slot;
            size_t roff0 = ent * 24;
            size_t roff1 = (ent + 1) * 24;
            uint64_t slot0 = got_addr + (ent * 8);
            uint64_t slot1 = got_addr + ((ent + 1) * 8);

            if (rela_dyn_buf != NULL && roff0 + 24 <= rela_dyn_sz) {
                write_u64_endian(rela_dyn_buf + roff0 + 0, e, slot0);
                write_u64_endian(rela_dyn_buf + roff0 + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_DTPMOD64);
                write_u64_endian(rela_dyn_buf + roff0 + 16, e, 0);
            }
            if (rela_dyn_buf != NULL && roff1 + 24 <= rela_dyn_sz) {
                write_u64_endian(rela_dyn_buf + roff1 + 0, e, slot1);
                write_u64_endian(rela_dyn_buf + roff1 + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_DTPOFF64);
                write_u64_endian(rela_dyn_buf + roff1 + 16, e, 0);
            }
        }
    }
    {
        size_t si;
        size_t extra_idx = 0;
        for (si = 0; si < elf_section_count(out); ++si) {
            elf_section_t *sec = elf_section_get(out, si);
            size_t rc;
            size_t ri;
            if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
                continue;
            }
            rc = elf_section_reloc_count(sec);
            for (ri = 0; ri < rc; ++ri) {
                const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
                const elf_symbol_t *sym;
                uint32_t type;
                uint32_t dynidx = 0;
                uint64_t slot_addr;
                int64_t addend = 0;
                size_t roff;
                uint64_t off;
                int emit = 0;
                int relative = 0;
                uint64_t relative_addend = 0;

                if (rel == NULL) {
                    continue;
                }
                sym = elf_reloc_symbol(rel);
                type = elf_reloc_type(rel);
                if (!reloc_is_x64_runtime_data_ref(type)) {
                    continue;
                }
                off = elf_reloc_offset(rel);
                slot_addr = elf_section_addr(sec) + off;
                if (elf_reloc_has_addend(rel)) {
                    addend = elf_reloc_addend(rel);
                } else {
                    const uint8_t *sbuf;
                    size_t ssz = 0;
                    sbuf = (const uint8_t *)elf_section_data(sec, &ssz);
                    if (sbuf == NULL || off + 8 > ssz) {
                        continue;
                    }
                    addend = (int64_t)read_u64_endian(sbuf + off, e);
                }
                if (is_runtime_import_symbol(sym) && import_is_canonical(imports, sym)) {
                    continue;
                }
                if (is_runtime_import_symbol(sym)) {
                    if (dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                        continue;
                    }
                    emit = 1;
                } else if (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym)) {
                    if (resolve_runtime_relative_addend(out, sym, addend, &relative_addend) != 0) {
                        continue;
                    }
                    relative = 1;
                    emit = 1;
                }
                if (!emit) {
                    continue;
                }
                roff = (rela_dyn_base_count + extra_idx) * 24;
                extra_idx++;
                if (rela_dyn_buf == NULL || roff + 24 > rela_dyn_sz) {
                    return -1;
                }
                write_u64_endian(rela_dyn_buf + roff + 0, e, slot_addr);
                if (relative) {
                    write_u64_endian(rela_dyn_buf + roff + 8, e, R_X86_64_RELATIVE);
                } else {
                    write_u64_endian(rela_dyn_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_64);
                }
                write_u64_endian(rela_dyn_buf + roff + 16, e, relative ? relative_addend : (uint64_t)addend);
            }
        }
        /* And the copies: "fill this from the shared object's own". */
        for (si = 0; si < imports->copy_count; ++si) {
            const elf_symbol_t *sym = imports->copies[si];
            size_t roff = (rela_dyn_base_count + extra_idx++) * 24;
            uint64_t addr = 0;
            uint32_t dynidx = 0;

            if (rela_dyn_buf == NULL || roff + 24 > rela_dyn_sz || resolve_symbol_addr(out, sym, 0, &addr, NULL) != 0 ||
                dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                fprintf(stderr, "ld: cannot make the copy relocation for %s\n", elf_symbol_name(sym));
                goto fail_import;
            }
            write_u64_endian(rela_dyn_buf + roff + 0, e, addr);
            write_u64_endian(rela_dyn_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_COPY);
            write_u64_endian(rela_dyn_buf + roff + 16, e, 0);
        }
    }

    if ((plt != NULL && elf_section_set_data(plt, plt_buf, plt_sz) != ELF_OK) ||
        (gotplt != NULL && elf_section_set_data(gotplt, gotplt_buf, gotplt_sz) != ELF_OK) ||
        (got != NULL && elf_section_set_data(got, got_buf, got_sz) != ELF_OK) ||
        (rela_plt != NULL && elf_section_set_data(rela_plt, rela_plt_buf, rela_plt_sz) != ELF_OK) ||
        (rela_dyn != NULL && elf_section_set_data(rela_dyn, rela_dyn_buf, rela_dyn_sz) != ELF_OK)) {
        free(plt_buf);
        free(gotplt_buf);
        free(got_buf);
        free(rela_plt_buf);
        free(rela_dyn_buf);
        return -1;
    }

    free(plt_buf);
    free(gotplt_buf);
    free(got_buf);
    free(rela_plt_buf);
    free(rela_dyn_buf);
    return 0;

fail_import:
    free(plt_buf);
    free(gotplt_buf);
    free(got_buf);
    free(rela_plt_buf);
    free(rela_dyn_buf);
    return -1;
}

int finalize_dynamic_imports_i386(elfobj_t *out, const dyn_import_vec_t *imports) {
    elf_section_t *plt;
    elf_section_t *gotplt;
    elf_section_t *got;
    elf_section_t *rel_plt;
    elf_section_t *rel_dyn;
    const elf_section_t *dynamic;
    uint8_t *plt_buf = NULL;
    uint8_t *gotplt_buf = NULL;
    uint8_t *got_buf = NULL;
    uint8_t *rel_plt_buf = NULL;
    uint8_t *rel_dyn_buf = NULL;
    size_t plt_sz = 0;
    size_t gotplt_sz = 0;
    size_t got_sz = 0;
    size_t rel_plt_sz = 0;
    size_t rel_dyn_sz = 0;
    size_t rel_dyn_base_count = 0;
    size_t runtime_extra_count = 0;
    size_t required_rel_dyn_sz = 0;
    size_t need_plt_count = 0;
    size_t i;
    uint64_t plt_addr;
    uint64_t gotplt_addr;
    uint64_t got_addr = 0;
    uint64_t dynamic_addr = 0;
    elfobj_endian_t e;
    int plt_pic_mode;

    if (out == NULL || imports == NULL) {
        return 0;
    }
    for (i = 0; i < imports->count; ++i) {
        if (imports->items[i].need_plt) {
            need_plt_count++;
        }
        if (imports->items[i].need_got) {
            rel_dyn_base_count++;
        }
        if (imports->items[i].need_tls_ie) {
            rel_dyn_base_count++;
        }
        if (imports->items[i].need_tls_gd) {
            rel_dyn_base_count += 2;
        }
    }
    runtime_extra_count = count_runtime_data_import_relocs_i386(out, imports) + imports->copy_count;
    required_rel_dyn_sz = (rel_dyn_base_count + runtime_extra_count) * 8;
    plt = elf_find_section(out, ".plt");
    gotplt = elf_find_section(out, ".got.plt");
    got = elf_find_section(out, ".got");
    rel_plt = elf_find_section(out, ".rel.plt");
    rel_dyn = elf_find_section(out, ".rel.dyn");
    dynamic = elf_find_section(out, ".dynamic");
    if (need_plt_count != 0 && (plt == NULL || gotplt == NULL)) {
        return -1;
    }

    plt_addr = plt != NULL ? elf_section_addr(plt) : 0;
    gotplt_addr = gotplt != NULL ? elf_section_addr(gotplt) : 0;
    if (got != NULL) {
        got_addr = elf_section_addr(got);
    }
    if (dynamic != NULL) {
        dynamic_addr = elf_section_addr(dynamic);
    }
    e = elf_endian(out);
    plt_pic_mode = elf_type(out) == ET_DYN ? 1 : 0;

    plt_sz = plt != NULL ? elf_section_size(plt) : 0;
    gotplt_sz = gotplt != NULL ? elf_section_size(gotplt) : 0;
    got_sz = got != NULL ? elf_section_size(got) : 0;
    rel_plt_sz = rel_plt != NULL ? elf_section_size(rel_plt) : 0;
    rel_dyn_sz = rel_dyn != NULL ? elf_section_size(rel_dyn) : 0;
    if (rel_dyn_sz < required_rel_dyn_sz) {
        rel_dyn_sz = required_rel_dyn_sz;
    }

    if (plt_sz != 0) {
        plt_buf = (uint8_t *)calloc(1, plt_sz);
        if (plt_buf == NULL) {
            return -1;
        }
    }
    if (gotplt_sz != 0) {
        gotplt_buf = (uint8_t *)calloc(1, gotplt_sz);
        if (gotplt_buf == NULL) {
            free(plt_buf);
            return -1;
        }
    }
    if (got_sz != 0) {
        got_buf = (uint8_t *)calloc(1, got_sz);
        if (got_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            return -1;
        }
    }
    if (rel_plt_sz != 0) {
        rel_plt_buf = (uint8_t *)calloc(1, rel_plt_sz);
        if (rel_plt_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            free(got_buf);
            return -1;
        }
    }
    if (rel_dyn_sz != 0) {
        rel_dyn_buf = (uint8_t *)calloc(1, rel_dyn_sz);
        if (rel_dyn_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            free(got_buf);
            free(rel_plt_buf);
            return -1;
        }
    }

    if (plt_buf != NULL && plt_sz >= 16) {
        if (plt_pic_mode) {
            /* PIC PLT0: pushl 4(%ebx); jmp *8(%ebx); nop*4 */
            plt_buf[0] = 0xff;
            plt_buf[1] = 0xb3;
            write_u32_endian(plt_buf + 2, e, 4);
            plt_buf[6] = 0xff;
            plt_buf[7] = 0xa3;
            write_u32_endian(plt_buf + 8, e, 8);
            plt_buf[12] = 0x90;
            plt_buf[13] = 0x90;
            plt_buf[14] = 0x90;
            plt_buf[15] = 0x90;
        } else {
            /* Non-PIC PLT0: pushl *GOT+4; jmp *GOT+8; nop*4 */
            plt_buf[0] = 0xff;
            plt_buf[1] = 0x35;
            write_u32_endian(plt_buf + 2, e, (uint32_t)(gotplt_addr + 4));
            plt_buf[6] = 0xff;
            plt_buf[7] = 0x25;
            write_u32_endian(plt_buf + 8, e, (uint32_t)(gotplt_addr + 8));
            plt_buf[12] = 0x90;
            plt_buf[13] = 0x90;
            plt_buf[14] = 0x90;
            plt_buf[15] = 0x90;
        }
    }
    if (gotplt_buf != NULL && gotplt_sz >= 12) {
        write_u32_endian(gotplt_buf + 0, e, (uint32_t)dynamic_addr);
        write_u32_endian(gotplt_buf + 4, e, 0);
        write_u32_endian(gotplt_buf + 8, e, 0);
    }

    for (i = 0; i < imports->count; ++i) {
        const dyn_import_t *imp = &imports->items[i];
        uint32_t dynidx = 0;
        if (dynsym_index_by_name(out, imp->name, &dynidx) != 0) {
            /* Its PLT entry and GOT slot would be left zero, to be
             * jumped through at run time. */
            fprintf(stderr, "ld: %s is imported and has no entry in the dynamic symbol table\n",
                    imp->name != NULL ? imp->name : "?");
            goto fail_import;
        }
        if (imp->need_plt) {
            size_t ent = imp->plt_slot;
            size_t poff = 16 + (ent * 16);
            size_t roff = ent * 8;
            uint64_t ent_addr = plt_addr + 16 + (ent * 16);
            uint64_t slot_addr = gotplt_addr + 12 + (ent * 4);
            int32_t rel;

            if (poff + 16 <= plt_sz) {
                if (plt_pic_mode) {
                    plt_buf[poff + 0] = 0xff;
                    plt_buf[poff + 1] = 0xa3;
                    write_u32_endian(plt_buf + poff + 2, e, (uint32_t)(12 + (ent * 4)));
                } else {
                    plt_buf[poff + 0] = 0xff;
                    plt_buf[poff + 1] = 0x25;
                    write_u32_endian(plt_buf + poff + 2, e, (uint32_t)slot_addr);
                }
                /* What is pushed for the resolver is where this entry's
                 * relocation is in .rel.plt, in bytes (the i386 psABI),
                 * not which entry it is. */
                plt_buf[poff + 6] = 0x68;
                write_u32_endian(plt_buf + poff + 7, e, (uint32_t)roff);
                plt_buf[poff + 11] = 0xe9;
                rel = (int32_t)((int64_t)plt_addr - (int64_t)(ent_addr + 16));
                write_u32_endian(plt_buf + poff + 12, e, (uint32_t)rel);
            }
            if ((12 + ((ent + 1) * 4)) <= gotplt_sz) {
                write_u32_endian(gotplt_buf + 12 + (ent * 4), e, (uint32_t)(ent_addr + 6));
            }
            if (rel_plt_buf != NULL && roff + 8 <= rel_plt_sz) {
                write_u32_endian(rel_plt_buf + roff + 0, e, (uint32_t)slot_addr);
                write_u32_endian(rel_plt_buf + roff + 4, e, (dynidx << 8) | R_386_JMP_SLOT);
            }
        }
        if (imp->need_got && got != NULL) {
            size_t ent = imp->got_slot;
            size_t roff = ent * 8;
            uint64_t slot_addr = got_addr + (ent * 4);

            if (rel_dyn_buf != NULL && roff + 8 <= rel_dyn_sz) {
                write_u32_endian(rel_dyn_buf + roff + 0, e, (uint32_t)slot_addr);
                write_u32_endian(rel_dyn_buf + roff + 4, e, (dynidx << 8) | R_386_GLOB_DAT);
            }
        }
        if (imp->need_tls_ie && got != NULL) {
            size_t ent = imp->tls_ie_slot;
            size_t roff = ent * 8;
            uint64_t slot_addr = got_addr + (ent * 4);

            if (rel_dyn_buf != NULL && roff + 8 <= rel_dyn_sz) {
                write_u32_endian(rel_dyn_buf + roff + 0, e, (uint32_t)slot_addr);
                /* The distance to add to the thread pointer, negative
                 * (TPOFF32 is the one to subtract, for code that says
                 * @gottpoff, which this slot is not for). */
                write_u32_endian(rel_dyn_buf + roff + 4, e, (dynidx << 8) | R_386_TLS_TPOFF);
            }
        }
        if (imp->need_tls_gd && got != NULL) {
            size_t ent = imp->tls_gd_slot;
            size_t roff0 = ent * 8;
            size_t roff1 = (ent + 1) * 8;
            uint64_t slot0 = got_addr + (ent * 4);
            uint64_t slot1 = got_addr + ((ent + 1) * 4);

            if (rel_dyn_buf != NULL && roff0 + 8 <= rel_dyn_sz) {
                write_u32_endian(rel_dyn_buf + roff0 + 0, e, (uint32_t)slot0);
                write_u32_endian(rel_dyn_buf + roff0 + 4, e, (dynidx << 8) | R_386_TLS_DTPMOD32);
            }
            if (rel_dyn_buf != NULL && roff1 + 8 <= rel_dyn_sz) {
                write_u32_endian(rel_dyn_buf + roff1 + 0, e, (uint32_t)slot1);
                write_u32_endian(rel_dyn_buf + roff1 + 4, e, (dynidx << 8) | R_386_TLS_DTPOFF32);
            }
        }
    }
    {
        size_t si;
        size_t extra_idx = 0;
        for (si = 0; si < elf_section_count(out); ++si) {
            elf_section_t *sec = elf_section_get(out, si);
            size_t rc;
            size_t ri;
            if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
                continue;
            }
            rc = elf_section_reloc_count(sec);
            for (ri = 0; ri < rc; ++ri) {
                const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
                const elf_symbol_t *sym;
                uint32_t type;
                uint32_t dynidx = 0;
                uint64_t slot_addr;
                size_t roff;
                int emit = 0;
                int relative = 0;

                if (rel == NULL) {
                    continue;
                }
                sym = elf_reloc_symbol(rel);
                type = elf_reloc_type(rel);
                if (!reloc_is_i386_runtime_data_ref(type)) {
                    continue;
                }
                if (is_runtime_import_symbol(sym) && import_is_canonical(imports, sym)) {
                    continue;
                }
                if (is_runtime_import_symbol(sym)) {
                    if (dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                        continue;
                    }
                    emit = 1;
                } else if (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym)) {
                    relative = 1;
                    emit = 1;
                }
                if (!emit) {
                    continue;
                }
                slot_addr = elf_section_addr(sec) + elf_reloc_offset(rel);
                roff = (rel_dyn_base_count + extra_idx) * 8;
                extra_idx++;
                if (rel_dyn_buf == NULL || roff + 8 > rel_dyn_sz) {
                    return -1;
                }
                write_u32_endian(rel_dyn_buf + roff + 0, e, (uint32_t)slot_addr);
                if (relative) {
                    write_u32_endian(rel_dyn_buf + roff + 4, e, R_386_RELATIVE);
                } else {
                    write_u32_endian(rel_dyn_buf + roff + 4, e, (dynidx << 8) | R_386_32);
                }
            }
        }
        /* And the copies: "fill this from the shared object's own". */
        for (si = 0; si < imports->copy_count; ++si) {
            const elf_symbol_t *sym = imports->copies[si];
            size_t roff = (rel_dyn_base_count + extra_idx++) * 8;
            uint64_t addr = 0;
            uint32_t dynidx = 0;

            if (rel_dyn_buf == NULL || roff + 8 > rel_dyn_sz || resolve_symbol_addr(out, sym, 0, &addr, NULL) != 0 ||
                dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                fprintf(stderr, "ld: cannot make the copy relocation for %s\n", elf_symbol_name(sym));
                goto fail_import;
            }
            write_u32_endian(rel_dyn_buf + roff + 0, e, (uint32_t)addr);
            write_u32_endian(rel_dyn_buf + roff + 4, e, (dynidx << 8) | R_386_COPY);
        }
    }

    if ((plt != NULL && elf_section_set_data(plt, plt_buf, plt_sz) != ELF_OK) ||
        (gotplt != NULL && elf_section_set_data(gotplt, gotplt_buf, gotplt_sz) != ELF_OK) ||
        (got != NULL && elf_section_set_data(got, got_buf, got_sz) != ELF_OK) ||
        (rel_plt != NULL && elf_section_set_data(rel_plt, rel_plt_buf, rel_plt_sz) != ELF_OK) ||
        (rel_dyn != NULL && elf_section_set_data(rel_dyn, rel_dyn_buf, rel_dyn_sz) != ELF_OK)) {
        free(plt_buf);
        free(gotplt_buf);
        free(got_buf);
        free(rel_plt_buf);
        free(rel_dyn_buf);
        return -1;
    }

    free(plt_buf);
    free(gotplt_buf);
    free(got_buf);
    free(rel_plt_buf);
    free(rel_dyn_buf);
    return 0;

fail_import:
    free(plt_buf);
    free(gotplt_buf);
    free(got_buf);
    free(rel_plt_buf);
    free(rel_dyn_buf);
    return -1;
}

/*
 * A text relocation is one the dynamic linker is left to apply to memory
 * that is mapped read-only.  Every object file has relocations in its
 * text, and nearly all are settled here; the ones that are not are the
 * direct references to what is only known at run time: something in a
 * shared object that has neither a copy nor a PLT entry for an address,
 * and, in a shared object or PIE, the address of anything at all.  The
 * name of the first read-only section that has one, or NULL.  It asks
 * what the import plan decided, so it is for use after that is made.
 */
const char *text_relocation_section(const ld_ctx_t *ctx, elfobj_t *out) {
    uint16_t machine = elf_machine(out);
    size_t si, ri;

    if (elf_type(out) != ET_DYN && ctx->dso_inputs.count == 0) {
        return NULL;
    }
    for (si = 0; si < elf_section_count(out); ++si) {
        elf_section_t *sec = elf_section_get(out, si);

        if (sec == NULL || (elf_section_flags(sec) & (SHF_ALLOC | SHF_WRITE)) != SHF_ALLOC) {
            continue;
        }
        for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym = rel != NULL ? elf_reloc_symbol(rel) : NULL;
            uint32_t type = rel != NULL ? elf_reloc_type(rel) : 0;

            if (rel == NULL ||
                !(machine == EM_X86_64 ? reloc_is_x64_runtime_data_ref(type) : reloc_is_i386_runtime_data_ref(type))) {
                continue;
            }
            if ((is_runtime_import_symbol(sym) && !import_is_canonical(&ctx->dyn_imports, sym)) ||
                (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym))) {
                return elf_section_name(sec) != NULL ? elf_section_name(sec) : "?";
            }
        }
    }
    return NULL;
}

/* The DT_SONAME of the shared object at `path`, to be freed; NULL if it
 * has none or cannot be read. */
char *dso_soname(const char *path) {
    strvec_t names;
    char *name;

    memset(&names, 0, sizeof(names));
    name = dso_dynamic_strings(path, DT_SONAME, &names) == 0 && names.count != 0 ? xstrdup(names.items[0]) : NULL;
    strvec_free(&names);
    return name;
}

/* The strings the dynamic section of the shared object at `path` gives
 * under `tag` (DT_SONAME, DT_NEEDED), added to `out`.  -1 if the object
 * cannot be read. */
int dso_dynamic_strings(const char *path, uint64_t want, strvec_t *out) {
    elfobj_t *obj = NULL;
    const elf_section_t *dynamic, *dynstr;
    const uint8_t *d, *s;
    size_t dsz = 0, ssz = 0, entsz, i;
    int rc = 0;

    if (elf_open(path, &obj) != ELF_OK) {
        return -1;
    }
    dynamic = elf_find_section(obj, ".dynamic");
    dynstr = elf_find_section(obj, ".dynstr");
    d = dynamic != NULL ? (const uint8_t *)elf_section_data(dynamic, &dsz) : NULL;
    s = dynstr != NULL ? (const uint8_t *)elf_section_data(dynstr, &ssz) : NULL;
    entsz = elf_class(obj) == ELFOBJ_CLASS_64 ? 16 : 8;
    for (i = 0; d != NULL && s != NULL && i + entsz <= dsz; i += entsz) {
        uint64_t tag = entsz == 16 ? read_u64_endian(d + i, elf_endian(obj)) : read_u32_endian(d + i, elf_endian(obj));
        uint64_t val = entsz == 16 ? read_u64_endian(d + i + 8, elf_endian(obj))
                                   : read_u32_endian(d + i + 4, elf_endian(obj));

        if (tag == DT_NULL) {
            break;
        }
        if (tag == want && val < ssz && memchr(s + val, '\0', ssz - (size_t)val) != NULL && s[val] != '\0' &&
            strvec_push(out, (const char *)s + val) != 0) {
            rc = -1;
            break;
        }
    }
    elf_close(obj);
    return rc;
}

int plan_dynamic_needed(ld_ctx_t *ctx, elfobj_t *out) {
    elf_section_t *dynstr;
    elf_section_t *dynsym;
    elf_section_t *versym_sec = NULL;
    elf_section_t *dynamic;
    elf_section_t *hash_sec = NULL;
    elf_section_t *gnu_hash_sec = NULL;
    elf_section_t *init_sec = NULL;
    elf_section_t *fini_sec = NULL;
    elf_section_t *init_array_sec = NULL;
    elf_section_t *fini_array_sec = NULL;
    uint8_t *dynstr_buf = NULL;
    uint8_t *dynsym_buf = NULL;
    uint8_t *versym_buf = NULL;
    uint8_t *dynamic_buf = NULL;
    uint8_t *hash_buf = NULL;
    uint8_t *gnu_hash_buf = NULL;
    size_t dynstr_len = 0;
    size_t dynstr_cap = 0;
    size_t dynsym_len = 0;
    size_t dynsym_cap = 0;
    size_t versym_len = 0;
    size_t versym_cap = 0;
    size_t dynamic_len = 0;
    size_t dynamic_cap = 0;
    size_t hash_sz = 0;
    size_t gnu_hash_sz = 0;
    size_t verdef_count = 0;
    size_t verneed_count = 0;
    int emit_versym = 0;
    size_t i;
    size_t entsz;
    int need_dyn;
    int textrel;
    elf_section_t *gotplt_sec = NULL;
    elf_section_t *rela_plt_sec = NULL;
    elf_section_t *rel_plt_sec = NULL;
    elf_section_t *rela_dyn_sec = NULL;
    elf_section_t *rel_dyn_sec = NULL;

    if (ctx == NULL || out == NULL) {
        return -1;
    }
    need_dyn = (elf_type(out) == ET_DYN) || (ctx->dso_inputs.count != 0) || ctx->export_dynamic;
    if (!need_dyn) {
        return 0;
    }

    dynstr = elf_find_section(out, ".dynstr");
    if (dynstr == NULL) {
        dynstr = elf_add_section(out, ".dynstr", SHT_STRTAB, SHF_ALLOC);
        if (dynstr == NULL) {
            return -1;
        }
    }
    if (elf_section_set_align(dynstr, 1) != ELF_OK) {
        return -1;
    }
    dynsym = elf_find_section(out, ".dynsym");
    if (dynsym == NULL) {
        dynsym = elf_add_section(out, ".dynsym", SHT_DYNSYM, SHF_ALLOC);
        if (dynsym == NULL) {
            return -1;
        }
    }
    if (elf_section_set_align(dynsym, elf_class(out) == ELFOBJ_CLASS_64 ? 8 : 4) != ELF_OK) {
        return -1;
    }
    if (ctx->hash_style == LD_HASH_SYSV || ctx->hash_style == LD_HASH_BOTH) {
        hash_sec = elf_find_section(out, ".hash");
        if (hash_sec == NULL) {
            hash_sec = elf_add_section(out, ".hash", SHT_HASH, SHF_ALLOC);
            if (hash_sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(hash_sec, 4) != ELF_OK) {
            return -1;
        }
    }
    if (ctx->hash_style == LD_HASH_GNU || ctx->hash_style == LD_HASH_BOTH) {
        gnu_hash_sec = elf_find_section(out, ".gnu.hash");
        if (gnu_hash_sec == NULL) {
            gnu_hash_sec = elf_add_section(out, ".gnu.hash", SHT_GNU_HASH, SHF_ALLOC);
            if (gnu_hash_sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(gnu_hash_sec, elf_class(out) == ELFOBJ_CLASS_64 ? 8 : 4) != ELF_OK) {
            return -1;
        }
    }
    dynamic = elf_find_section(out, ".dynamic");
    if (dynamic == NULL) {
        dynamic = elf_add_section(out, ".dynamic", SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE);
        if (dynamic == NULL) {
            return -1;
        }
    }
    if (elf_section_set_align(dynamic, elf_class(out) == ELFOBJ_CLASS_64 ? 8 : 4) != ELF_OK) {
        return -1;
    }
    init_sec = elf_find_section(out, ".init");
    fini_sec = elf_find_section(out, ".fini");
    init_array_sec = elf_find_section(out, ".init_array");
    fini_array_sec = elf_find_section(out, ".fini_array");
    gotplt_sec = elf_find_section(out, ".got.plt");
    rela_plt_sec = elf_find_section(out, ".rela.plt");
    rel_plt_sec = elf_find_section(out, ".rel.plt");
    rela_dyn_sec = elf_find_section(out, ".rela.dyn");
    rel_dyn_sec = elf_find_section(out, ".rel.dyn");

    if (dynbuf_append(&dynstr_buf, &dynstr_len, &dynstr_cap, "\0", 1) != 0) {
        free(dynstr_buf);
        return -1;
    }

    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        /* A library is needed by the name it gives itself: what it was
         * found as at link time (libc.so) is a link to it, and need not
         * exist where the program runs. */
        uint32_t off = 0;

        if (dynstr_append_cstr(&dynstr_buf, &dynstr_len, &dynstr_cap, dso_needed_name(ctx, i), &off) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_NEEDED, off) != 0) {
            free(dynstr_buf);
            free(dynamic_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (ctx->soname != NULL || ctx->rpaths.count != 0) {
        char *runpath = NULL;
        size_t len = 0;
        uint32_t off = 0;
        int bad = 0;

        for (i = 0; i < ctx->rpaths.count; ++i) {
            len += strlen(ctx->rpaths.items[i]) + 1;
        }
        if (len != 0) {
            runpath = (char *)calloc(1, len);
            bad = runpath == NULL;
            for (i = 0; !bad && i < ctx->rpaths.count; ++i) {
                snprintf(runpath + strlen(runpath), len - strlen(runpath), "%s%s", i != 0 ? ":" : "",
                         ctx->rpaths.items[i]);
            }
        }
        bad = bad ||
              (ctx->soname != NULL &&
               (dynstr_append_cstr(&dynstr_buf, &dynstr_len, &dynstr_cap, ctx->soname, &off) != 0 ||
                dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                     elf_class(out), elf_endian(out), DT_SONAME, off) != 0)) ||
              (runpath != NULL &&
               (dynstr_append_cstr(&dynstr_buf, &dynstr_len, &dynstr_cap, runpath, &off) != 0 ||
                dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                     elf_class(out), elf_endian(out), DT_RUNPATH, off) != 0));
        free(runpath);
        if (bad) {
            free(dynstr_buf);
            free(dynamic_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    if (init_sec != NULL && elf_section_size(init_sec) != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_INIT, 0) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (fini_sec != NULL && elf_section_size(fini_sec) != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_FINI, 0) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (init_array_sec != NULL && elf_section_size(init_array_sec) != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_INIT_ARRAY, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_INIT_ARRAYSZ,
                                 elf_section_size(init_array_sec)) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (fini_array_sec != NULL && elf_section_size(fini_array_sec) != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_FINI_ARRAY, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_FINI_ARRAYSZ,
                                 elf_section_size(fini_array_sec)) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (hash_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_HASH, 0) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (gnu_hash_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_GNU_HASH, 0) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 24 : 16;
    dynsym_buf = (uint8_t *)calloc(1, entsz);
    if (dynsym_buf == NULL) {
        free(dynstr_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    dynsym_len = entsz;
    dynsym_cap = entsz;
    {
        uint8_t v0[2];
        write_u16_endian(v0, elf_endian(out), VER_NDX_LOCAL);
        if (dynbuf_append(&versym_buf, &versym_len, &versym_cap, v0, sizeof(v0)) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    for (i = 0; i < elf_symbol_count(out); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(out, i);
        const char *emit_name = NULL;
        char *emit_name_tmp = NULL;
        uint8_t entry[24];
        uint8_t info;
        uint8_t other;
        uint32_t name_off = 0;
        uint16_t shndx;
        uint16_t version;
        uint64_t value;
        uint64_t size;

        if (!dynsym_should_export(ctx, out, sym)) {
            continue;
        }
        emit_name = elf_symbol_name(sym);
        if (emit_name != NULL && elf_symbol_version(sym) > VER_NDX_GLOBAL) {
            const char *base_name;
            size_t base_len;
            const char *ver_name;
            int is_default_name;
            split_symbol_version(emit_name, &base_name, &base_len, &ver_name, &is_default_name);
            if (ver_name != NULL && base_name != NULL && base_len != strlen(emit_name)) {
                emit_name_tmp = (char *)malloc(base_len + 1);
                if (emit_name_tmp == NULL) {
                    free(dynstr_buf);
                    free(dynsym_buf);
                    free(dynamic_buf);
                    free(versym_buf);
                    free(hash_buf);
                    free(gnu_hash_buf);
                    return -1;
                }
                memcpy(emit_name_tmp, base_name, base_len);
                emit_name_tmp[base_len] = '\0';
                emit_name = emit_name_tmp;
            }
        }
        if (dynstr_append_cstr(&dynstr_buf, &dynstr_len, &dynstr_cap, emit_name, &name_off) != 0) {
            free(emit_name_tmp);
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
        free(emit_name_tmp);

        info = (uint8_t)(((elf_symbol_bind(sym) & 0x0f) << 4) | (elf_symbol_type(sym) & 0x0f));
        other = (uint8_t)(elf_symbol_visibility(sym) & 0x03);
        shndx = elf_symbol_shndx(sym);
        value = elf_symbol_value(sym);
        size = elf_symbol_size(sym);
        memset(entry, 0, sizeof(entry));
        if (elf_class(out) == ELFOBJ_CLASS_64) {
            write_u32_endian(entry + 0, elf_endian(out), name_off);
            entry[4] = info;
            entry[5] = other;
            write_u16_endian(entry + 6, elf_endian(out), shndx);
            write_u64_endian(entry + 8, elf_endian(out), value);
            write_u64_endian(entry + 16, elf_endian(out), size);
        } else {
            write_u32_endian(entry + 0, elf_endian(out), name_off);
            write_u32_endian(entry + 4, elf_endian(out), (uint32_t)value);
            write_u32_endian(entry + 8, elf_endian(out), (uint32_t)size);
            entry[12] = info;
            entry[13] = other;
            write_u16_endian(entry + 14, elf_endian(out), shndx);
        }
        if (dynbuf_append(&dynsym_buf, &dynsym_len, &dynsym_cap, entry, entsz) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
        version = elf_symbol_version(sym);
        if (version == 0) {
            version = VER_NDX_GLOBAL;
        }
        {
            uint8_t vraw[2];
            write_u16_endian(vraw, elf_endian(out), version);
            if (dynbuf_append(&versym_buf, &versym_len, &versym_cap, vraw, sizeof(vraw)) != 0) {
                free(dynstr_buf);
                free(dynsym_buf);
                free(dynamic_buf);
                free(versym_buf);
                free(hash_buf);
                free(gnu_hash_buf);
                return -1;
            }
        }
    }

    if (plan_symbol_version_sections(ctx, out, &dynstr_buf, &dynstr_len, &dynstr_cap,
                                     dynsym_buf, dynsym_len, entsz, versym_buf, versym_len,
                                     &verdef_count, &verneed_count) != 0) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    /*
     * "name@VERSION" is how an object file spells a versioned symbol, and
     * how the names went into the string table, for the version numbers
     * to be given out from.  They have been: the dynamic symbol table has
     * the name alone, the version being the number beside it in
     * .gnu.version, and the hash tables made next are of names alone.
     * A symbol left as "thing@VERS_1" was one no reference to "thing"
     * could find.
     */
    {
        size_t k;

        for (k = 1; k * entsz + 4 <= dynsym_len; ++k) {
            uint32_t noff = read_u32_endian(dynsym_buf + k * entsz, elf_endian(out));
            uint8_t *at = noff < dynstr_len
                              ? (uint8_t *)memchr(dynstr_buf + noff, '@', strnlen((const char *)dynstr_buf + noff, dynstr_len - noff))
                              : NULL;

            if (at != NULL) {
                *at = '\0';
            }
        }
    }
    emit_versym = dynsym_len > entsz || verdef_count != 0 || verneed_count != 0;
    if (emit_versym) {
        versym_sec = elf_find_section(out, ".gnu.version");
        if (versym_sec == NULL) {
            versym_sec = elf_add_section(out, ".gnu.version", SHT_GNU_versym, SHF_ALLOC);
            if (versym_sec == NULL) {
                free(dynstr_buf);
                free(dynsym_buf);
                free(dynamic_buf);
                free(versym_buf);
                free(hash_buf);
                free(gnu_hash_buf);
                return -1;
            }
        }
        if (elf_section_set_align(versym_sec, 2) != ELF_OK) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    if (hash_sec != NULL) {
        hash_buf = build_sysv_hash_section(dynsym_buf, dynsym_len, dynstr_buf, dynstr_len,
                                           entsz, elf_endian(out), &hash_sz);
        if (hash_buf == NULL) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (gnu_hash_sec != NULL) {
        gnu_hash_buf = build_gnu_hash_section(dynsym_buf, dynsym_len, dynstr_buf, dynstr_len,
                                              entsz, elf_class(out), elf_endian(out), &gnu_hash_sz);
        if (gnu_hash_buf == NULL) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            return -1;
        }
    }

    if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_STRTAB, 0) != 0 ||
        dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_SYMTAB, 0) != 0 ||
        dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_STRSZ, dynstr_len) != 0 ||
        dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_SYMENT, entsz) != 0) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    /*
     * -z now, said the three ways dynamic linkers look for it; and that
     * the dynamic linker will have to write on what is mapped read-only,
     * said the two ways, where the output has such relocations.
     */
    textrel = text_relocation_section(ctx, out) != NULL;
    if ((ctx->z_now &&
         dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                              elf_class(out), elf_endian(out), DT_BIND_NOW, 0) != 0) ||
        /* DF_1_PIE is how a PIE is told from a shared object, both being
         * ET_DYN. */
        ((ctx->z_now || ctx->pie) &&
         dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                              elf_class(out), elf_endian(out), DT_FLAGS_1,
                              (ctx->z_now ? DF_1_NOW : 0) | (ctx->pie ? DF_1_PIE : 0)) != 0) ||
        (textrel &&
         dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                              elf_class(out), elf_endian(out), DT_TEXTREL, 0) != 0) ||
        ((ctx->z_now || textrel) &&
         dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                              elf_class(out), elf_endian(out), DT_FLAGS,
                              (ctx->z_now ? DF_BIND_NOW : 0) | (textrel ? DF_TEXTREL : 0)) != 0)) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_DEBUG, 0) != 0) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    if (emit_versym) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERSYM, 0) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (verdef_count != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERDEF, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERDEFNUM, verdef_count) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (verneed_count != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERNEED, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERNEEDNUM, verneed_count) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    if (gotplt_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_PLTGOT, 0) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (rela_plt_sec != NULL || rel_plt_sec != NULL) {
        uint64_t pltrel_type = rela_plt_sec != NULL ? DT_RELA : DT_REL;
        uint64_t pltrel_sz = rela_plt_sec != NULL ? elf_section_size(rela_plt_sec) : elf_section_size(rel_plt_sec);
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_PLTREL, pltrel_type) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_PLTRELSZ, pltrel_sz) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_JMPREL, 0) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (rela_dyn_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELA, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELASZ, elf_section_size(rela_dyn_sec)) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELAENT,
                                 elf_class(out) == ELFOBJ_CLASS_64 ? 24u : 12u) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    } else if (rel_dyn_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_REL, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELSZ, elf_section_size(rel_dyn_sec)) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELENT,
                                 elf_class(out) == ELFOBJ_CLASS_64 ? 16u : 8u) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_NULL, 0) != 0) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }

    if (elf_section_set_data(dynstr, dynstr_buf, dynstr_len) != ELF_OK ||
        elf_section_set_data(dynsym, dynsym_buf, dynsym_len) != ELF_OK ||
        (emit_versym && elf_section_set_data(versym_sec, versym_buf, versym_len) != ELF_OK) ||
        elf_section_set_data(dynamic, dynamic_buf, dynamic_len) != ELF_OK ||
        (hash_sec != NULL && elf_section_set_data(hash_sec, hash_buf, hash_sz) != ELF_OK) ||
        (gnu_hash_sec != NULL && elf_section_set_data(gnu_hash_sec, gnu_hash_buf, gnu_hash_sz) != ELF_OK)) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    free(dynstr_buf);
    free(dynsym_buf);
    free(dynamic_buf);
    free(versym_buf);
    free(hash_buf);
    free(gnu_hash_buf);
    return 0;
}

int patch_dynamic_tag_values(elfobj_t *out) {
    elf_section_t *dynamic;
    elf_section_t *dynstr;
    elf_section_t *dynsym;
    elf_section_t *hash;
    elf_section_t *gnu_hash;
    elf_section_t *init_sec;
    elf_section_t *fini_sec;
    elf_section_t *init_array_sec;
    elf_section_t *fini_array_sec;
    elf_section_t *versym;
    elf_section_t *verdef;
    elf_section_t *verneed;
    elf_section_t *gotplt;
    elf_section_t *rela_plt;
    elf_section_t *rel_plt;
    elf_section_t *rela_dyn;
    elf_section_t *rel_dyn;
    size_t dyn_sz = 0;
    const uint8_t *dyn_data;
    uint8_t *buf;
    size_t i;
    size_t entsz;
    uint64_t dynstr_addr = 0;
    uint64_t dynsym_addr = 0;
    uint64_t hash_addr = 0;
    uint64_t gnu_hash_addr = 0;
    uint64_t init_addr = 0;
    uint64_t fini_addr = 0;
    uint64_t init_array_addr = 0;
    uint64_t init_array_size = 0;
    uint64_t fini_array_addr = 0;
    uint64_t fini_array_size = 0;
    uint64_t dynstr_size = 0;
    uint64_t dynsym_entsz;
    uint64_t versym_addr = 0;
    uint64_t verdef_addr = 0;
    uint64_t verneed_addr = 0;
    uint64_t gotplt_addr = 0;
    uint64_t jmprel_addr = 0;
    uint64_t jmprel_size = 0;
    uint64_t rela_addr = 0;
    uint64_t rela_size = 0;
    uint64_t rel_addr = 0;
    uint64_t rel_size = 0;

    if (out == NULL) {
        return -1;
    }
    dynamic = elf_find_section(out, ".dynamic");
    if (dynamic == NULL) {
        return 0;
    }
    dynstr = elf_find_section(out, ".dynstr");
    dynsym = elf_find_section(out, ".dynsym");
    hash = elf_find_section(out, ".hash");
    gnu_hash = elf_find_section(out, ".gnu.hash");
    init_sec = elf_find_section(out, ".init");
    fini_sec = elf_find_section(out, ".fini");
    init_array_sec = elf_find_section(out, ".init_array");
    fini_array_sec = elf_find_section(out, ".fini_array");
    versym = elf_find_section(out, ".gnu.version");
    verdef = elf_find_section(out, ".gnu.version_d");
    verneed = elf_find_section(out, ".gnu.version_r");
    gotplt = elf_find_section(out, ".got.plt");
    rela_plt = elf_find_section(out, ".rela.plt");
    rel_plt = elf_find_section(out, ".rel.plt");
    rela_dyn = elf_find_section(out, ".rela.dyn");
    rel_dyn = elf_find_section(out, ".rel.dyn");
    if (dynstr == NULL || dynsym == NULL) {
        return -1;
    }
    dynstr_addr = elf_section_addr(dynstr);
    dynsym_addr = elf_section_addr(dynsym);
    if (hash != NULL) {
        hash_addr = elf_section_addr(hash);
    }
    if (gnu_hash != NULL) {
        gnu_hash_addr = elf_section_addr(gnu_hash);
    }
    if (init_sec != NULL) {
        init_addr = elf_section_addr(init_sec);
    }
    if (fini_sec != NULL) {
        fini_addr = elf_section_addr(fini_sec);
    }
    if (init_array_sec != NULL) {
        init_array_addr = elf_section_addr(init_array_sec);
        init_array_size = elf_section_size(init_array_sec);
    }
    if (fini_array_sec != NULL) {
        fini_array_addr = elf_section_addr(fini_array_sec);
        fini_array_size = elf_section_size(fini_array_sec);
    }
    dynstr_size = elf_section_size(dynstr);
    dynsym_entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 24u : 16u;
    if (versym != NULL) {
        versym_addr = elf_section_addr(versym);
    }
    if (verdef != NULL) {
        verdef_addr = elf_section_addr(verdef);
    }
    if (verneed != NULL) {
        verneed_addr = elf_section_addr(verneed);
    }
    if (gotplt != NULL) {
        gotplt_addr = elf_section_addr(gotplt);
    }
    if (rela_plt != NULL) {
        jmprel_addr = elf_section_addr(rela_plt);
        jmprel_size = elf_section_size(rela_plt);
    } else if (rel_plt != NULL) {
        jmprel_addr = elf_section_addr(rel_plt);
        jmprel_size = elf_section_size(rel_plt);
    }
    if (rela_dyn != NULL) {
        rela_addr = elf_section_addr(rela_dyn);
        rela_size = elf_section_size(rela_dyn);
    }
    if (rel_dyn != NULL) {
        rel_addr = elf_section_addr(rel_dyn);
        rel_size = elf_section_size(rel_dyn);
    }

    dyn_data = (const uint8_t *)elf_section_data(dynamic, &dyn_sz);
    if (dyn_sz == 0 || dyn_data == NULL) {
        return -1;
    }
    entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 16 : 8;
    if ((dyn_sz % entsz) != 0) {
        return -1;
    }
    buf = (uint8_t *)malloc(dyn_sz);
    if (buf == NULL) {
        return -1;
    }
    memcpy(buf, dyn_data, dyn_sz);

    for (i = 0; i < dyn_sz; i += entsz) {
        int64_t tag;
        uint8_t *p = buf + i;

        if (elf_class(out) == ELFOBJ_CLASS_64) {
            tag = (int64_t)read_u64_endian(p + 0, elf_endian(out));
        } else {
            tag = (int64_t)(int32_t)read_u32_endian(p + 0, elf_endian(out));
        }
        if (tag == DT_STRTAB) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), dynstr_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)dynstr_addr);
            }
        } else if (tag == DT_HASH) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), hash_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)hash_addr);
            }
        } else if (tag == DT_GNU_HASH) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), gnu_hash_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)gnu_hash_addr);
            }
        } else if (tag == DT_INIT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), init_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)init_addr);
            }
        } else if (tag == DT_FINI) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), fini_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)fini_addr);
            }
        } else if (tag == DT_INIT_ARRAY) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), init_array_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)init_array_addr);
            }
        } else if (tag == DT_INIT_ARRAYSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), init_array_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)init_array_size);
            }
        } else if (tag == DT_FINI_ARRAY) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), fini_array_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)fini_array_addr);
            }
        } else if (tag == DT_FINI_ARRAYSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), fini_array_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)fini_array_size);
            }
        } else if (tag == DT_SYMTAB) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), dynsym_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)dynsym_addr);
            }
        } else if (tag == DT_STRSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), dynstr_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)dynstr_size);
            }
        } else if (tag == DT_SYMENT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), dynsym_entsz);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)dynsym_entsz);
            }
        } else if (tag == DT_PLTGOT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), gotplt_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)gotplt_addr);
            }
        } else if (tag == DT_VERSYM) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), versym_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)versym_addr);
            }
        } else if (tag == DT_VERDEF) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), verdef_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)verdef_addr);
            }
        } else if (tag == DT_VERNEED) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), verneed_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)verneed_addr);
            }
        } else if (tag == DT_JMPREL) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), jmprel_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)jmprel_addr);
            }
        } else if (tag == DT_PLTRELSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), jmprel_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)jmprel_size);
            }
        } else if (tag == DT_RELA) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), rela_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)rela_addr);
            }
        } else if (tag == DT_RELASZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), rela_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)rela_size);
            }
        } else if (tag == DT_RELAENT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), elf_class(out) == ELFOBJ_CLASS_64 ? 24u : 12u);
            } else {
                write_u32_endian(p + 4, elf_endian(out), elf_class(out) == ELFOBJ_CLASS_64 ? 24u : 12u);
            }
        } else if (tag == DT_REL) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), rel_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)rel_addr);
            }
        } else if (tag == DT_RELSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), rel_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)rel_size);
            }
        } else if (tag == DT_RELENT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), elf_class(out) == ELFOBJ_CLASS_64 ? 16u : 8u);
            } else {
                write_u32_endian(p + 4, elf_endian(out), elf_class(out) == ELFOBJ_CLASS_64 ? 16u : 8u);
            }
        }
    }

    if (elf_section_set_data(dynamic, buf, dyn_sz) != ELF_OK) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

int finalize_symbol_values_for_output(elfobj_t *out) {
    uint64_t tls_start = 0;
    size_t i;

    if (out == NULL) {
        return -1;
    }
    if (elf_type(out) != ET_EXEC && elf_type(out) != ET_DYN) {
        return 0;
    }
    for (i = 0; i < elf_symbol_count(out); ++i) {
        elf_symbol_t *sym = elf_symbol_at(out, i);
        uint16_t shndx;
        uint64_t value;
        uint64_t sec_addr;

        if (sym == NULL) {
            continue;
        }
        shndx = elf_symbol_shndx(sym);
        if (shndx == SHN_UNDEF || shndx == SHN_ABS || shndx == SHN_COMMON || shndx >= 0xff00) {
            continue;
        }
        if (shndx == 0 || (size_t)(shndx - 1) >= elf_section_count(out)) {
            return -1;
        }
        value = elf_symbol_value(sym);
        sec_addr = elf_section_addr(elf_section_get(out, (size_t)(shndx - 1)));
        if (value > UINT64_MAX - sec_addr) {
            return -1;
        }
        /* A thread-local symbol's value is where it is in a thread's
         * copy, there being no one address it is at. */
        if (elf_symbol_type(sym) == STT_TLS && tls_extent(out, &tls_start, NULL, NULL, NULL)) {
            sec_addr -= tls_start;
        }
        if (elf_symbol_set_value(sym, value + sec_addr) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

int patch_dynsym_symbol_values(const ld_ctx_t *ctx, elfobj_t *out) {
    elf_section_t *dynsym;
    size_t dyn_sz = 0;
    const uint8_t *src;
    uint8_t *buf;
    size_t entsz;
    size_t nslots;
    size_t slot = 1;
    size_t i;

    if (ctx == NULL || out == NULL) {
        return -1;
    }
    dynsym = elf_find_section(out, ".dynsym");
    if (dynsym == NULL) {
        return 0;
    }
    src = (const uint8_t *)elf_section_data(dynsym, &dyn_sz);
    if (src == NULL || dyn_sz == 0) {
        return -1;
    }
    entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 24 : 16;
    if ((dyn_sz % entsz) != 0) {
        return -1;
    }
    nslots = dyn_sz / entsz;
    buf = (uint8_t *)malloc(dyn_sz);
    if (buf == NULL) {
        return -1;
    }
    memcpy(buf, src, dyn_sz);

    for (i = 0; i < elf_symbol_count(out); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(out, i);
        uint64_t value;
        size_t off;

        if (!dynsym_should_export(ctx, out, sym)) {
            continue;
        }
        if (slot >= nslots) {
            free(buf);
            return -1;
        }
        value = elf_symbol_value(sym);
        if (elf_symbol_shndx(sym) == SHN_UNDEF && import_is_canonical(&ctx->dyn_imports, sym)) {
            /* Undefined, with a value: its PLT entry here is its address
             * for every module. */
            const dyn_import_t *imp = find_planned_import(ctx, elf_symbol_name(sym));
            const elf_section_t *plt = elf_find_section(out, ".plt");

            if (imp == NULL || plt == NULL) {
                free(buf);
                return -1;
            }
            value = elf_section_addr(plt) + 16 + (imp->plt_slot * 16);
        }
        off = slot * entsz;
        if (elf_class(out) == ELFOBJ_CLASS_64) {
            write_u64_endian(buf + off + 8, elf_endian(out), value);
        } else {
            write_u32_endian(buf + off + 4, elf_endian(out), (uint32_t)value);
        }
        slot++;
    }

    if (elf_section_set_data(dynsym, buf, dyn_sz) != ELF_OK) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}
