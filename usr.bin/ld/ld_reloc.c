/*
 * ld_reloc.c -- relocation: addresses of symbols, the i386 GOT, applying.
 */

#include "ld.h"

int apply_defsyms(ld_ctx_t *ctx, elfobj_t *out) {
    size_t i;

    for (i = 0; i < ctx->defsyms.count; ++i) {
        const char *name = ctx->defsyms.items[i].name;
        uint64_t value = ctx->defsyms.items[i].value;
        elf_symbol_t *sym = elf_find_symbol(out, name);

        if (sym == NULL) {
            sym = elf_add_symbol(out, name, value, 0, STB_GLOBAL, STT_NOTYPE);
            if (sym == NULL) {
                fprintf(stderr, "ld: failed to create --defsym symbol `%s`\n", name);
                return -1;
            }
        }
        if (elf_symbol_set_value(sym, value) != ELF_OK || elf_symbol_set_shndx(sym, SHN_ABS) != ELF_OK) {
            fprintf(stderr, "ld: failed to apply --defsym `%s`\n", name);
            return -1;
        }
    }
    return 0;
}

static int64_t sign_extend_u64(uint64_t v, int bits) {
    uint64_t m;
    if (bits <= 0 || bits >= 64) {
        return (int64_t)v;
    }
    m = 1ULL << (bits - 1);
    return (int64_t)((v ^ m) - m);
}

static int reloc_addend_is_signed(uint16_t machine, uint32_t type) {
    if (elf_reloc_is_pc_relative_for_machine(machine, type)) {
        return 1;
    }
    switch (machine) {
    case EM_386:
        switch (type) {
        case R_386_GOT32:
        case R_386_GOTOFF:
        case R_386_TLS_TPOFF:
        case R_386_TLS_IE:
        case R_386_TLS_GOTIE:
        case R_386_TLS_LE:
        case R_386_TLS_GD:
        case R_386_TLS_LDM:
        case R_386_TLS_LDO_32:
            return 1;
        default:
            return 0;
        }
    case EM_X86_64:
        switch (type) {
        case R_X86_64_32S:
        case R_X86_64_TLSGD:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_TPOFF32:
        case R_X86_64_TLSLD:
        case R_X86_64_DTPOFF32:
            return 1;
        default:
            return 0;
        }
    default:
        return 1;
    }
}

static uint64_t read_uint_bytes(const uint8_t *p, int sz, elfobj_endian_t e) {
    uint64_t v = 0;
    int i;
    if (e == ELFOBJ_ENDIAN_BE) {
        for (i = 0; i < sz; ++i) {
            v = (v << 8) | (uint64_t)p[i];
        }
    } else {
        for (i = sz - 1; i >= 0; --i) {
            v = (v << 8) | (uint64_t)p[i];
        }
    }
    return v;
}

static void write_uint_bytes(uint8_t *p, int sz, elfobj_endian_t e, uint64_t v) {
    int i;
    if (e == ELFOBJ_ENDIAN_BE) {
        for (i = sz - 1; i >= 0; --i) {
            p[i] = (uint8_t)(v & 0xffu);
            v >>= 8;
        }
    } else {
        for (i = 0; i < sz; ++i) {
            p[i] = (uint8_t)(v & 0xffu);
            v >>= 8;
        }
    }
}

int resolve_symbol_addr(elfobj_t *obj, const elf_symbol_t *sym, int allow_undef,
                               uint64_t *out_addr, const char **undef_name) {
    uint16_t shndx;
    uint8_t bind;
    uint64_t value;

    if (sym == NULL) {
        *out_addr = 0;
        return 0;
    }

    shndx = elf_symbol_shndx(sym);
    bind = elf_symbol_bind(sym);
    value = elf_symbol_value(sym);
    if (shndx == SHN_UNDEF) {
        if (allow_undef || bind == STB_WEAK) {
            *out_addr = 0;
            return 0;
        }
        if (undef_name != NULL) {
            *undef_name = elf_symbol_name(sym);
        }
        return -1;
    }
    if (shndx == SHN_ABS || shndx == SHN_COMMON || shndx >= 0xff00) {
        *out_addr = value;
        return 0;
    }
    if (shndx == 0 || (size_t)(shndx - 1) >= elf_section_count(obj)) {
        return -1;
    }
    *out_addr = elf_section_addr(elf_section_get(obj, (size_t)(shndx - 1))) + value;
    return 0;
}

const dyn_import_t *find_planned_import(const ld_ctx_t *ctx, const char *name) {
    int idx;

    if (ctx == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    idx = dyn_import_find(&ctx->dyn_imports, name);
    if (idx < 0) {
        return NULL;
    }
    return &ctx->dyn_imports.items[idx];
}

static int resolve_symbol_addr_for_reloc(elfobj_t *obj, const ld_ctx_t *ctx, const elf_symbol_t *sym,
                                         int allow_undef, uint32_t type, uint64_t *out_addr,
                                         const char **undef_name) {
    const dyn_import_t *imp;
    const char *name;
    elf_section_t *sec;

    if (sym == NULL || out_addr == NULL) {
        return resolve_symbol_addr(obj, sym, allow_undef, out_addr, undef_name);
    }
    if (elf_symbol_shndx(sym) != SHN_UNDEF) {
        return resolve_symbol_addr(obj, sym, allow_undef, out_addr, undef_name);
    }
    name = elf_symbol_name(sym);
    imp = find_planned_import(ctx, name);
    if (imp == NULL) {
        /* The table's own name is nobody's import and no input defines
         * it: it is where the table is, when there is one. */
        if (name != NULL && strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0 &&
            (elf_find_section(obj, ".got.plt") != NULL ||
             elf_find_section(obj, ".got") != NULL)) {
            sec = elf_find_section(obj, ".got.plt");
            if (sec == NULL) {
                sec = elf_find_section(obj, ".got");
            }
            *out_addr = elf_section_addr(sec);
            return 0;
        }
        return resolve_symbol_addr(obj, sym, allow_undef, out_addr, undef_name);
    }
    if (ctx != NULL && ctx->mode == 64) {
        int plt_ref = reloc_is_x64_plt_ref(type);
        if (!plt_ref && type == R_X86_64_PC32 &&
            (elf_symbol_type(sym) == STT_FUNC || elf_symbol_type(sym) == STT_NOTYPE)) {
            plt_ref = 1;
        }
        plt_ref |= imp->canonical && reloc_is_direct_ref(EM_X86_64, type, 0);
        if (plt_ref && imp->need_plt) {
            sec = elf_find_section(obj, ".plt");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + 16 + (imp->plt_slot * 16);
            return 0;
        }
        if (reloc_is_x64_got_ref(type)) {
            if (imp->need_got) {
                sec = elf_find_section(obj, ".got");
                if (sec == NULL) {
                    return -1;
                }
                *out_addr = elf_section_addr(sec) + (imp->got_slot * 8);
                return 0;
            }
            if (imp->need_plt) {
                sec = elf_find_section(obj, ".got.plt");
                if (sec == NULL) {
                    return -1;
                }
                *out_addr = elf_section_addr(sec) + 24 + (imp->plt_slot * 8);
                return 0;
            }
        }
        if (reloc_is_x64_tls_gd_ref(type) && imp->need_tls_gd) {
            sec = elf_find_section(obj, ".got");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + (imp->tls_gd_slot * 8);
            return 0;
        }
        if (reloc_is_x64_tls_ie_ref(type) && imp->need_tls_ie) {
            sec = elf_find_section(obj, ".got");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + (imp->tls_ie_slot * 8);
            return 0;
        }
    } else if (ctx != NULL && ctx->mode == 32) {
        if ((reloc_is_i386_plt_ref(type) || (imp->canonical && reloc_is_direct_ref(EM_386, type, 0))) &&
            imp->need_plt) {
            sec = elf_find_section(obj, ".plt");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + 16 + (imp->plt_slot * 16);
            return 0;
        }
        if (reloc_is_i386_got_ref(type)) {
            if (imp->need_got) {
                sec = elf_find_section(obj, ".got");
                if (sec == NULL) {
                    return -1;
                }
                *out_addr = elf_section_addr(sec) + (imp->got_slot * 4);
                return 0;
            }
            if (imp->need_plt) {
                sec = elf_find_section(obj, ".got.plt");
                if (sec == NULL) {
                    return -1;
                }
                *out_addr = elf_section_addr(sec) + 12 + (imp->plt_slot * 4);
                return 0;
            }
        }
        if (reloc_is_i386_tls_gd_ref(type) && imp->need_tls_gd) {
            sec = elf_find_section(obj, ".got");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + (imp->tls_gd_slot * 4);
            return 0;
        }
        if (reloc_is_i386_tls_ie_ref(type) && imp->need_tls_ie) {
            sec = elf_find_section(obj, ".got");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + (imp->tls_ie_slot * 4);
            return 0;
        }
    }
    if (name != NULL && strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0) {
        sec = elf_find_section(obj, ".got.plt");
        if (sec == NULL) {
            sec = elf_find_section(obj, ".got");
        }
        if (sec != NULL) {
            *out_addr = elf_section_addr(sec);
            return 0;
        }
    }
    return resolve_symbol_addr(obj, sym, allow_undef, out_addr, undef_name);
}

static int can_defer_runtime_reloc(const ld_ctx_t *ctx, uint16_t machine, uint32_t type, const elf_symbol_t *sym) {
    const dyn_import_t *imp;

    if (ctx == NULL || sym == NULL || !is_runtime_import_symbol(sym)) {
        return 0;
    }
    if (machine == EM_X86_64 && !reloc_is_x64_runtime_data_ref(type)) {
        return 0;
    }
    if (machine == EM_386 && !reloc_is_i386_runtime_data_ref(type)) {
        return 0;
    }
    if (machine != EM_X86_64 && machine != EM_386) {
        return 0;
    }
    imp = find_planned_import(ctx, elf_symbol_name(sym));
    return imp != NULL && !imp->canonical;
}

/*
 * The global offset table of an i386 link, for what is defined in it.
 *
 * Position-independent i386 code finds its data through %ebx, which it
 * loads with the address of the GOT (R_386_GOTPC), and then either adds an
 * offset from there (R_386_GOTOFF) or loads an address out of a slot of
 * the table (R_386_GOT32, R_386_GOT32X).  A dynamic link has had a .got
 * since there were imports to put in it.  A static link had none, and a
 * symbol defined in the output never had a slot in either: the C startup
 * file is position-independent, so that was every link.
 *
 * plan_local_got_i386() runs before addresses are assigned.  If anything
 * is GOT-relative and there is no table, it makes a .got, with a slot for
 * each defined symbol a GOT32-class relocation names; fill_local_got_i386()
 * stores the addresses once they are known.  Where the table is the
 * imports' own, sized by other code, a reference to a defined symbol is
 * instead turned into a direct one when it is applied (the `mov` of a
 * GOT32X becomes a `lea`), which is what the X in GOT32X permits.
 */
static int reloc_is_i386_got_slot(uint32_t type) {
    return type == R_386_GOT32 || type == R_386_GOT32X;
}

static int reloc_is_i386_got_relative(uint32_t type) {
    return type == R_386_GOTPC || type == R_386_GOTOFF ||
           reloc_is_i386_got_slot(type);
}

static uint64_t i386_got_base(elfobj_t *obj) {
    elf_section_t *sec = elf_find_section(obj, ".got.plt");

    if (sec == NULL) {
        sec = elf_find_section(obj, ".got");
    }
    return sec != NULL ? elf_section_addr(sec) : 0;
}

static long local_got_slot_lookup(const ld_ctx_t *ctx, const elf_symbol_t *sym) {
    size_t i;

    for (i = 0; i < ctx->local_got_count; ++i) {
        if (ctx->local_got[i] == sym) {
            return (long)i;
        }
    }
    return -1;
}

/* The slot of `sym` in the table this link made, or -1. */
static long local_got_slot(const ld_ctx_t *ctx, const elf_symbol_t *sym) {
    if (ctx == NULL || !ctx->local_got_owned) {
        return -1;
    }
    return local_got_slot_lookup(ctx, sym);
}

int plan_local_got_i386(ld_ctx_t *ctx, elfobj_t *out) {
    size_t si, ri;
    int need_base = 0;
    elf_section_t *got;

    if (ctx == NULL || out == NULL || elf_machine(out) != EM_386) {
        return 0;
    }
    ctx->local_got_count = 0;
    ctx->local_got_owned = 0;
    for (si = 0; si < elf_section_count(out); ++si) {
        elf_section_t *sec = elf_section_get(out, si);

        if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
            continue;
        }
        for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym = elf_reloc_symbol(rel);
            uint32_t type = elf_reloc_type(rel);

            if (!reloc_is_i386_got_relative(type)) {
                continue;
            }
            need_base = 1;
            /*
             * A slot holds an address, and in a shared object or a PIE an
             * address is not known until it is loaded: a slot there would
             * need a relocation of its own for the dynamic linker, which
             * is not made.  Such an output gets the table for a base and
             * no slots, and its references to what it defines are turned
             * into direct ones as they are applied.
             */
            if (elf_type(out) == ET_DYN || !reloc_is_i386_got_slot(type) || sym == NULL ||
                elf_symbol_shndx(sym) == SHN_UNDEF ||
                local_got_slot_lookup(ctx, sym) >= 0) {
                continue;
            }
            if (ctx->local_got_count == ctx->local_got_cap) {
                size_t ncap = ctx->local_got_cap ? ctx->local_got_cap * 2 : 16;
                const elf_symbol_t **n = (const elf_symbol_t **)
                    realloc((void *)ctx->local_got, ncap * sizeof(n[0]));

                if (n == NULL) {
                    return -1;
                }
                ctx->local_got = n;
                ctx->local_got_cap = ncap;
            }
            ctx->local_got[ctx->local_got_count++] = sym;
        }
    }
    if (!need_base || elf_find_section(out, ".got") != NULL ||
        elf_find_section(out, ".got.plt") != NULL) {
        return 0;               /* nothing to do, or the imports' table */
    }
    got = elf_add_section(out, ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    if (got == NULL || elf_section_set_align(got, 4) != ELF_OK ||
        set_section_zero_data(got, 4 * (ctx->local_got_count ? ctx->local_got_count : 1)) != 0) {
        return -1;
    }
    ctx->local_got_owned = 1;
    return 0;
}

int fill_local_got_i386(const ld_ctx_t *ctx, elfobj_t *out) {
    elf_section_t *got;
    uint8_t *buf;
    size_t i, sz;
    int rc;

    if (ctx == NULL || !ctx->local_got_owned || ctx->local_got_count == 0) {
        return 0;
    }
    got = elf_find_section(out, ".got");
    sz = 4 * ctx->local_got_count;
    buf = (uint8_t *)calloc(1, sz);
    if (got == NULL || buf == NULL) {
        free(buf);
        return -1;
    }
    for (i = 0; i < ctx->local_got_count; ++i) {
        uint64_t addr = 0;
        const char *undef = NULL;

        if (resolve_symbol_addr(out, ctx->local_got[i], 0, &addr, &undef) != 0) {
            free(buf);
            return -1;
        }
        write_uint_bytes(buf + 4 * i, 4, elf_endian(out), addr);
    }
    rc = elf_section_set_data(got, buf, sz) == ELF_OK ? 0 : -1;
    free(buf);
    return rc;
}

/*
 * A reference to a thread-local variable that the program itself defines.
 *
 * The program's thread-local storage is the first there is, at a distance
 * from the thread pointer that is known now (tls_tpoff), so however the
 * compiler was told to reach the variable -- by the distance directly
 * (local-exec), or by a distance to be read from a GOT slot that the
 * dynamic linker would fill in (initial-exec) -- it comes to the distance,
 * as a constant in the instruction.  For initial-exec that means making
 * the instruction that loads from the slot into one that loads a constant,
 * as the psABI's TLS supplement lays out; the instruction is the same
 * length and the register the same.
 *
 * Returns 0 when done, -1 with *why set.
 */
static int apply_tls_in_program(const elfobj_t *obj, uint16_t machine, uint32_t type, uint8_t *buf, size_t sec_sz,
                                uint64_t off, uint64_t S, int64_t addend, const char **why) {
    elfobj_endian_t e = elf_endian(obj);

    (void)sec_sz;
    if (machine == EM_386) {
        uint32_t tpoff = (uint32_t)tls_tpoff(obj, S + (uint64_t)addend);
        uint8_t modrm = off >= 1 ? buf[off - 1] : 0;
        uint8_t reg = (uint8_t)((modrm >> 3) & 7);

        switch (type) {
            case R_386_TLS_LE:              /* sym@ntpoff: added to %gs:0 */
                break;
            case R_386_TLS_LE_32:           /* sym@tpoff: subtracted from it */
                tpoff = 0U - tpoff;
                break;
            case R_386_TLS_IE:              /* sym@indntpoff: the slot, by address */
                if (off >= 1 && buf[off - 1] == 0xa1) {
                    buf[off - 1] = 0xb8;                        /* mov $x,%eax */
                } else if (off >= 2 && buf[off - 2] == 0x8b && (modrm & 0xc7) == 0x05) {
                    buf[off - 2] = 0xc7;                        /* mov $x,%reg */
                    buf[off - 1] = (uint8_t)(0xc0 | reg);
                } else if (off >= 2 && buf[off - 2] == 0x03 && (modrm & 0xc7) == 0x05) {
                    buf[off - 2] = 0x81;                        /* add $x,%reg */
                    buf[off - 1] = (uint8_t)(0xc0 | reg);
                } else {
                    *why = "an initial-exec reference in an instruction that is neither mov nor add";
                    return -1;
                }
                break;
            case R_386_TLS_GOTIE:           /* sym@gotntpoff(%reg): the slot, from the GOT */
                if (off >= 2 && buf[off - 2] == 0x8b && (modrm & 0xc0) == 0x80) {
                    buf[off - 2] = 0xc7;                        /* mov $x,%reg */
                    buf[off - 1] = (uint8_t)(0xc0 | reg);
                } else if (off >= 2 && buf[off - 2] == 0x03 && (modrm & 0xc0) == 0x80) {
                    buf[off - 2] = 0x8d;                        /* lea x(%reg),%reg */
                    buf[off - 1] = (uint8_t)(0x80 | (reg << 3) | reg);
                } else {
                    *why = "an initial-exec reference in an instruction that is neither mov nor add";
                    return -1;
                }
                break;
            default:
                *why = "this way of reaching thread-local storage is not supported in a program";
                return -1;
        }
        write_uint_bytes(buf + off, 4, e, tpoff);
        return 0;
    }
    if (machine == EM_X86_64) {
        switch (type) {
            case R_X86_64_TPOFF32:
                write_uint_bytes(buf + off, 4, e, (uint32_t)tls_tpoff(obj, S + (uint64_t)addend));
                return 0;
            case R_X86_64_TPOFF64:
                write_uint_bytes(buf + off, 8, e, (uint64_t)tls_tpoff(obj, S + (uint64_t)addend));
                return 0;
            case R_X86_64_GOTTPOFF: {       /* sym@gottpoff(%rip); the addend is the -4 of %rip */
                uint8_t rex, op, modrm, reg;

                if (off < 3 || (buf[off - 1] & 0xc7) != 0x05 || (buf[off - 3] != 0x48 && buf[off - 3] != 0x4c)) {
                    *why = "an initial-exec reference in an instruction that is neither mov nor add";
                    return -1;
                }
                rex = buf[off - 3];
                op = buf[off - 2];
                modrm = buf[off - 1];
                reg = (uint8_t)((modrm >> 3) & 7);
                /* REX.R named the register as the destination of a load;
                 * as the operand of an immediate it is REX.B that does. */
                if (op == 0x8b) {
                    buf[off - 3] = rex == 0x4c ? 0x49 : 0x48;   /* mov $x,%reg */
                    buf[off - 2] = 0xc7;
                    buf[off - 1] = (uint8_t)(0xc0 | reg);
                } else if (op == 0x03 && reg == 4) {
                    buf[off - 3] = rex == 0x4c ? 0x49 : 0x48;   /* add $x,%rsp or %r12 */
                    buf[off - 2] = 0x81;
                    buf[off - 1] = (uint8_t)(0xc0 | reg);
                } else if (op == 0x03) {
                    buf[off - 3] = rex == 0x4c ? 0x4d : 0x48;   /* lea x(%reg),%reg */
                    buf[off - 2] = 0x8d;
                    buf[off - 1] = (uint8_t)(0x80 | (reg << 3) | reg);
                } else {
                    *why = "an initial-exec reference in an instruction that is neither mov nor add";
                    return -1;
                }
                write_uint_bytes(buf + off, 4, e, (uint32_t)tls_tpoff(obj, S + (uint64_t)addend + 4));
                return 0;
            }
            default:
                break;
        }
    }
    *why = "this way of reaching thread-local storage is not supported in a program";
    return -1;
}

int apply_all_relocations(elfobj_t *obj, const ld_ctx_t *ctx, int allow_undefined) {
    size_t i;
    static int trace_reloc_env = -1;

    if (trace_reloc_env < 0) {
        const char *v = getenv("LD_DEBUG_RELOC_TRACE");
        trace_reloc_env = (v != NULL && v[0] != '\0') ? 1 : 0;
    }
    elfobj_endian_t endian = elf_endian(obj);
    uint16_t machine = elf_machine(obj);

    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        uint64_t flags = sec != NULL ? elf_section_flags(sec) : 0;
        uint8_t *buf;
        const void *src;
        size_t sec_sz;
        size_t rc;
        size_t ri;

        if (sec == NULL) {
            continue;
        }
        /* Sections that are not loaded are relocated like the rest: the
         * debugging information is all addresses of the program, and it
         * was left pointing at nothing, with its relocations copied into
         * the executable beside it. */
        rc = elf_section_reloc_count(sec);
        if (rc == 0) {
            continue;
        }
        if (elf_section_type(sec) == SHT_NOBITS) {
            fprintf(stderr, "ld: relocations against NOBITS section '%s' unsupported\n",
                    elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
            return -1;
        }
        src = elf_section_data(sec, &sec_sz);
        if (src == NULL || sec_sz == 0) {
            fprintf(stderr, "ld: relocation target section '%s' has no data\n",
                    elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
            return -1;
        }
        buf = (uint8_t *)malloc(sec_sz);
        if (buf == NULL) {
            return -1;
        }
        memcpy(buf, src, sec_sz);

        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const char *sec_name = elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>";
            uint64_t off;
            uint32_t type;
            int64_t addend;
            int width;
            const elf_symbol_t *sym;
            const char *sym_name = "<none>";
            uint64_t S = 0;
            uint64_t P;
            uint64_t outv = 0;
            const char *undef_name = NULL;
            elf_err_t err;

            if (rel == NULL) {
                continue;
            }
            off = elf_reloc_offset(rel);
            type = elf_reloc_type(rel);
            width = elf_reloc_size_for_machine(elf_machine(obj), type);
            if (width <= 0 || width > 8) {
                free(buf);
                fprintf(stderr,
                        "ld: relocation error: section=%s offset=0x%llx type=%u symbol=%s: unsupported relocation width\n",
                        sec_name, (unsigned long long)off, type, sym_name);
                return -1;
            }
            if (off + (uint64_t)width > sec_sz) {
                free(buf);
                fprintf(stderr,
                        "ld: relocation error: section=%s offset=0x%llx type=%u symbol=%s: relocation out of range\n",
                        sec_name, (unsigned long long)off, type, sym_name);
                return -1;
            }
            if (elf_reloc_has_addend(rel)) {
                addend = elf_reloc_addend(rel);
            } else {
                uint64_t raw = read_uint_bytes(buf + off, width, endian);
                if (reloc_addend_is_signed(machine, type)) {
                    addend = sign_extend_u64(raw, width * 8);
                } else {
                    addend = (int64_t)raw;
                }
            }

            sym = elf_reloc_symbol(rel);
            if (sym != NULL && elf_symbol_name(sym) != NULL && elf_symbol_name(sym)[0] != '\0') {
                sym_name = elf_symbol_name(sym);
            }
            if (resolve_symbol_addr_for_reloc(obj, ctx, sym, allow_undefined, type, &S, &undef_name) != 0) {
                if ((flags & SHF_ALLOC) == 0) {
                    /* What a description of the program refers to may
                     * have been left out of it; the description then
                     * says 0, and nothing that runs depends on it. */
                    continue;
                }
                if (can_defer_runtime_reloc(ctx, machine, type, sym)) {
                    if (trace_reloc_env) {
                        fprintf(stderr,
                                "ld: reloc-trace: deferred runtime relocation section=%s off=0x%llx type=%u sym=%s\n",
                                sec_name, (unsigned long long)off, (unsigned)type,
                                undef_name != NULL ? undef_name : sym_name);
                    }
                    continue;
                }
                free(buf);
                fprintf(stderr,
                        "ld: relocation error: section=%s offset=0x%llx type=%u symbol=%s: unresolved relocation symbol\n",
                        sec_name, (unsigned long long)off, type, undef_name != NULL ? undef_name : sym_name);
                return -1;
            }
            P = elf_section_addr(sec) + off;
            if (machine == EM_386 && reloc_is_i386_got_relative(type)) {
                /*
                 * The i386 relocations that are relative to the table
                 * (psABI: GOTPC = GOT + A - P, GOTOFF = S + A - GOT,
                 * GOT32 and GOT32X = G + A - GOT, G the address of the
                 * symbol's slot).  The library's arithmetic has S, A and
                 * P to work with and made each of them S + A.
                 */
                uint64_t got = i386_got_base(obj);
                int defined = sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF;
                long slot = defined ? local_got_slot(ctx, sym) : -1;
                const char *why = NULL;

                if (type == R_386_GOTPC) {
                    outv = got + (uint64_t)addend - P;
                } else if (type == R_386_GOTOFF) {
                    outv = S + (uint64_t)addend - got;
                } else if (!defined) {
                    /* An import: S is its slot already. */
                    outv = S + (uint64_t)addend - got;
                } else if (slot >= 0) {
                    elf_section_t *gs = elf_find_section(obj, ".got");

                    outv = elf_section_addr(gs) + 4U * (uint64_t)slot +
                           (uint64_t)addend - got;
                } else if (type == R_386_GOT32X && off >= 2 &&
                           buf[off - 2] == 0x8b && (buf[off - 1] & 0xc0) == 0x80) {
                    /* mov sym@GOT(%reg), %reg -> lea sym@GOTOFF(%reg), %reg */
                    buf[off - 2] = 0x8d;
                    outv = S + (uint64_t)addend - got;
                } else {
                    why = "a reference through the GOT to a symbol with no slot in it";
                }
                if (got == 0 && why == NULL) {
                    why = "GOT-relative relocation and no GOT";
                }
                if (why != NULL) {
                    free(buf);
                    fprintf(stderr,
                            "ld: relocation error: section=%s offset=0x%llx type=%s symbol=%s: %s\n",
                            sec_name, (unsigned long long)off,
                            elf_reloc_name_for_machine(machine, type), sym_name, why);
                    return -1;
                }
                write_uint_bytes(buf + off, width, endian, outv & 0xffffffffULL);
                continue;
            }
            if (elf_reloc_is_tls_for_machine(machine, type) && sym != NULL &&
                elf_symbol_shndx(sym) != SHN_UNDEF) {
                const char *why = NULL;
                int done;

                if ((flags & SHF_ALLOC) == 0) {
                    /* A description of the program says where in a
                     * thread's copy the variable is. */
                    uint64_t start = 0;

                    (void)tls_extent(obj, &start, NULL, NULL, NULL);
                    write_uint_bytes(buf + off, width, endian, S + (uint64_t)addend - start);
                    continue;
                }
                if (elf_type(obj) == ET_DYN && !ctx->pie) {
                    why = "thread-local storage defined in a shared object is not supported";
                    done = -1;
                } else {
                    done = apply_tls_in_program(obj, machine, type, buf, sec_sz, off, S, addend, &why);
                }
                if (done < 0) {
                    free(buf);
                    fprintf(stderr,
                            "ld: relocation error: section=%s offset=0x%llx type=%s symbol=%s: %s\n",
                            sec_name, (unsigned long long)off,
                            elf_reloc_name_for_machine(machine, type), sym_name, why);
                    return -1;
                }
                continue;
            }
            err = elf_apply_relocation_value(obj, type, P, S, addend, &outv);
            if (err != ELF_OK) {
                free(buf);
                fprintf(stderr,
                        "ld: relocation error: section=%s offset=0x%llx type=%u symbol=%s: %s\n",
                        sec_name, (unsigned long long)off, type, sym_name, elf_errstr(err));
                return -1;
            }
            if (machine == EM_X86_64 &&
                (type == R_X86_64_GOTPCREL ||
                 type == R_X86_64_GOTPCRELX ||
                 type == R_X86_64_REX_GOTPCRELX) &&
                sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF &&
                off >= 2 && buf[off - 2] == 0x8b) {
                /*
                 * We currently materialize GOTPCREL-family relocations with
                 * S+A-P math in elf_reloc.c. For resolved/non-preemptible
                 * symbols that is only valid when we relax MOV mem->reg into
                 * LEA so the instruction yields the symbol address.
                 */
                buf[off - 2] = 0x8d;
            }
            if (trace_reloc_env) {
                fprintf(stderr,
                        "ld: reloc-trace: section=%s off=0x%llx type=%u sym=%s P=0x%llx S=0x%llx A=%lld out=0x%llx\n",
                        sec_name != NULL ? sec_name : "<unnamed>",
                        (unsigned long long)off,
                        (unsigned)type,
                        sym_name != NULL ? sym_name : "<null>",
                        (unsigned long long)P,
                        (unsigned long long)S,
                        (long long)addend,
                        (unsigned long long)outv);
            }
            write_uint_bytes(buf + off, width, endian, outv);
        }

        if (elf_section_set_data(sec, buf, sec_sz) != ELF_OK) {
            free(buf);
            return -1;
        }
        free(buf);
        /* Applied, and what the dynamic linker has to do has its own
         * records by now; an executable or shared object does not carry
         * the linker's. */
        if (elf_type(obj) != ET_REL && !ctx->emit_relocs &&
            elf_section_clear_relocations(sec) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}
