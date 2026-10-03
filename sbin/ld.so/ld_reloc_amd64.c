/*
 * ld_reloc_amd64.c - the amd64 relocation types (RELA: the addend A is
 * r_addend, and the word being relocated is overwritten, never read).
 *
 *   R_X86_64_NONE      - no-op.
 *   R_X86_64_RELATIVE  - *p = B + A            (already done in
 *                        ld_start_amd64.S for our own image).
 *   R_X86_64_GLOB_DAT  - *p = S
 *   R_X86_64_JUMP_SLOT - *p = S  (bound on first call when the object
 *                        allows it - see "Lazy binding" below)
 *   R_X86_64_64        - *p = S + A
 *   R_X86_64_PC32      - *(u32 *)p = S + A - P
 *   R_X86_64_COPY      - copy the symbol's bytes from the providing DSO
 *   R_X86_64_TPOFF64   - *p = offset of the variable from %fs
 *   R_X86_64_DTPMOD64, R_X86_64_DTPOFF64 - the two words of a tls_index
 *
 *   R_X86_64_IRELATIVE - *p = the value returned by the resolver at B + A
 *
 * Indirect functions.  IRELATIVE, and a GLOB_DAT / JUMP_SLOT / 64 whose
 * symbol resolves to an STT_GNU_IFUNC definition, store what a resolver
 * function returns.  The resolver is ordinary code in some object, so it
 * is not called from the main pass, where that object may not be
 * relocated yet: the entry is skipped, the object marked has_ifunc, and
 * ld_relocate_ifunc() comes back for it with ifunc_pass set once every
 * object is relocated.  In that pass everything else is left alone.
 *
 * Lazy binding.  An object linked without -z now has, in each PLT slot
 * of .got.plt, the link-time address of that slot's stub in .plt; the
 * stub pushes the slot's index into DT_JMPREL and jumps to PLT0, which
 * pushes GOT[1] and jumps through GOT[2].  ld_reloc_lazy_setup() biases
 * the slots, puts the object in GOT[1] and ld_plt_trampoline in GOT[2],
 * and the first call of each function then arrives in ld_plt_fixup(),
 * which resolves the symbol, stores it in the slot and returns it for
 * the trampoline to jump to.  An undefined function is therefore
 * reported when it is first called, not when the object is loaded.
 * Binding is eager instead when the object asks for it (DT_BIND_NOW,
 * DF_BIND_NOW, DF_1_NOW - which is how the system libraries are built),
 * when LD_BIND_NOW is set, and for what a dlopen(RTLD_NOW) loads.
 *
 * See ld_reloc.c for the table walk and the ordering of the passes.
 */

#include "ld.h"

/* Diagnose a reference to a symbol nothing defines.  Returns -1. */
static int undefined_symbol(const ld_obj_t *obj, const char *what,
                            const char *name) {
    ld_puts("ld64.so: undefined "); ld_puts(what); ld_puts(": ");
    ld_puts(name); ld_puts(" in "); ld_puts(obj->name); ld_puts("\n");
    return -1;
}

/* A TLS relocation against a module that has no place in the static TLS
 * block.  Its tls_offset is 0, so computing with it would yield offsets
 * at or above the thread pointer - over the TCB and the DTV. */
static int tls_not_laid_out(const ld_obj_t *def) {
    ld_puts("ld64.so: TLS module has no static TLS slot: ");
    ld_puts(def->name); ld_puts("\n");
    return -1;
}

/*
 * Find the module and the offset within its TLS image that a TLS
 * relocation of `obj` against symbol `sym` refers to.  sym == 0 is a
 * local variable of obj itself (the offset is then entirely in the
 * addend); a defined symbol is obj's own; an undefined one is looked up
 * in the other modules.  Returns 0, or -1 after a diagnostic.
 */
static int tls_target(const ld_obj_t *obj, ld_u32 sym,
                      const ld_obj_t **def_out, ld_addr *val_out) {
    *def_out = obj;
    *val_out = 0;
    if (sym == 0) return 0;

    const Elf_Sym *s = &obj->symtab[sym];
    if (s->st_shndx != SHN_UNDEF) {
        *val_out = s->st_value;
        return 0;
    }
    const char *nm = obj->strtab + s->st_name;
    const ld_obj_t *def = 0;
    ld_addr val = ld_resolve_tls(nm, obj, &def);
    if (!def) return undefined_symbol(obj, "TLS symbol", nm);
    *def_out = def;
    *val_out = val;
    return 0;
}

int ld_reloc_apply(ld_obj_t *obj, Elf_Reloc *r) {
    ld_u32   type = ELF_R_TYPE(r->r_info);
    ld_u32   sym  = ELF_R_SYM(r->r_info);
    ld_addr *p    = (ld_addr *)(r->r_offset + obj->base);
    ld_addr  a    = (ld_addr)r->r_addend;

    /* The indirect-function pass revisits only what the main pass
     * deferred to it. */
    if (obj->ifunc_pass) {
        switch (type) {
        case R_X86_64_IRELATIVE:
        case R_X86_64_GLOB_DAT:
        case R_X86_64_64:
            break;
        case R_X86_64_JUMP_SLOT:
            /* Lazily bound slots stay with ld_plt_fixup. */
            if (obj->lazy) return 0;
            break;
        default:
            return 0;
        }
        if (type != R_X86_64_IRELATIVE && sym == 0) return 0;
    }

    switch (type) {
    case R_X86_64_NONE:
        return 0;

    case R_X86_64_RELATIVE:
        *p = obj->base + a;
        return 0;

    case R_X86_64_IRELATIVE:
        if (!obj->ifunc_pass) { obj->has_ifunc = 1; return 0; }
        *p = ld_ifunc_call(obj->base + a);
        return 0;

    case R_X86_64_GLOB_DAT:
    case R_X86_64_JUMP_SLOT:
    case R_X86_64_64: {
        if (sym == 0) {
            if (type != R_X86_64_64) {
                ld_puts("ld64.so: GLOB_DAT/JUMP_SLOT with sym=0 in ");
                ld_puts(obj->name); ld_puts("\n");
                return -1;
            }
            *p = obj->base + a;
            return 0;
        }
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        int ifunc = 0;
        ld_addr v = ld_reloc_resolve_ifunc(obj, sym, name, &ifunc);
        if (v == 0) {
            /* Weak undefined symbols are allowed to remain 0. */
            unsigned char bind = ELF_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) { *p = 0; return 0; }
            return undefined_symbol(obj, "symbol", name);
        }
        if (ifunc) {
            if (!obj->ifunc_pass) { obj->has_ifunc = 1; return 0; }
            v = ld_ifunc_call(v);
        } else if (obj->ifunc_pass) {
            return 0;               /* applied by the main pass */
        }
        /* The psABI gives GLOB_DAT and JUMP_SLOT no addend. */
        *p = (type == R_X86_64_64) ? v + a : v;
        return 0;
    }

    case R_X86_64_PC32: {
        if (sym == 0) return 0;
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        ld_addr v = ld_reloc_resolve(obj, sym, name);
        if (v == 0) {
            unsigned char bind = ELF_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) return 0;
            return undefined_symbol(obj, "R_X86_64_PC32", name);
        }
        ld_addr disp = v + a - (ld_addr)p;          /* S + A - P */
        /* The field is 32 bits; a target further than 2 GiB away cannot
         * be reached, and truncating would jump somewhere else. */
        if ((ld_i64)disp != (ld_i64)(ld_i32)disp) {
            ld_puts("ld64.so: R_X86_64_PC32 out of range: "); ld_puts(name);
            ld_puts(" in "); ld_puts(obj->name); ld_puts("\n");
            return -1;
        }
        *(ld_u32 *)p = (ld_u32)disp;
        return 0;
    }

    case R_X86_64_COPY: {
        /* Find the source-of-truth in any OTHER loaded object and
         * copy the symbol's bytes into our slot. */
        if (sym == 0) return 0;
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        ld_addr size = 0;
        ld_addr src = ld_resolve_with_size(name, obj, &size);
        if (src == 0) {
            unsigned char bind = ELF_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) return 0;
            return undefined_symbol(obj, "R_X86_64_COPY", name);
        }
        /* Copy at most the destination's own size.  Normally both
         * sides agree (same extern data), but a version-skewed
         * provider whose symbol grew would otherwise overrun the
         * executable's .bss slot; clamp to the smaller and warn. */
        ld_addr dstsize = obj->symtab[sym].st_size;
        ld_addr n = size;
        if (dstsize && dstsize < n) {
            ld_puts("ld64.so: R_X86_64_COPY size mismatch for ");
            ld_puts(name); ld_puts(" - clamping to destination\n");
            n = dstsize;
        }
        unsigned char *dst = (unsigned char *)p;
        const unsigned char *s = (const unsigned char *)src;
        for (ld_addr i = 0; i < n; i++) dst[i] = s[i];
        return 0;
    }

    case R_X86_64_TPOFF64: {
        /* Initial-exec: the variable's offset from the thread pointer.
         * Variant II puts the module's block tls_offset bytes BELOW the
         * thread pointer, so the result is negative: code then reads
         * %fs:(that offset). */
        const ld_obj_t *def;
        ld_addr val;
        if (tls_target(obj, sym, &def, &val) != 0) return -1;
        if (def->tls_memsz == 0) {
            ld_puts("ld64.so: TPOFF64 against an object with no PT_TLS: ");
            ld_puts(def->name); ld_puts("\n");
            return -1;
        }
        if (def->tls_modid == 0) return tls_not_laid_out(def);
        *p = val + a - def->tls_offset;
        return 0;
    }

    case R_X86_64_DTPMOD64: {
        /* Module-id word of the tls_index __tls_get_addr takes: the
         * DEFINING module's id (obj's own for a local-dynamic
         * reference, where sym == 0). */
        const ld_obj_t *def;
        ld_addr val;
        if (tls_target(obj, sym, &def, &val) != 0) return -1;
        if (def->tls_modid == 0) return tls_not_laid_out(def);
        *p = def->tls_modid;
        return 0;
    }

    case R_X86_64_DTPOFF64: {
        /* Offset-within-module word of the same pair. */
        const ld_obj_t *def;
        ld_addr val;
        if (tls_target(obj, sym, &def, &val) != 0) return -1;
        *p = val + a;
        return 0;
    }

    default:
        ld_puts("ld64.so: unsupported reloc type "); ld_putd(type);
        ld_puts(" in "); ld_puts(obj->name); ld_puts("\n");
        return -1;
    }
}

int ld_reloc_lazy_setup(ld_obj_t *obj) {
    if (ld_bind_now || obj->bind_now || !obj->pltgot) return 0;

    /* Only a table of plain PLT slots, each still holding its stub's
     * address, can be left for later; anything else is bound now. */
    ld_addr n = obj->pltrelsz / sizeof(Elf_Reloc);
    for (ld_addr i = 0; i < n; i++) {
        const Elf_Reloc *r = &obj->jmprel[i];
        ld_u32 type = ELF_R_TYPE(r->r_info);
        if (type == R_X86_64_IRELATIVE) continue;
        if (type != R_X86_64_JUMP_SLOT) return 0;
        if (*(ld_addr *)(r->r_offset + obj->base) == 0) return 0;
    }

    for (ld_addr i = 0; i < n; i++) {
        const Elf_Reloc *r = &obj->jmprel[i];
        if (ELF_R_TYPE(r->r_info) == R_X86_64_IRELATIVE) {
            obj->has_ifunc = 1;     /* ld_relocate_ifunc fills the slot */
            continue;
        }
        *(ld_addr *)(r->r_offset + obj->base) += obj->base;
    }
    obj->pltgot[1] = (ld_addr)obj;
    obj->pltgot[2] = (ld_addr)ld_plt_trampoline;
    obj->lazy = 1;
    return 1;
}

ld_addr ld_plt_fixup(ld_obj_t *obj, ld_addr idx) {
    /* The lookup walks the loaded-object list, which a dlopen in another
     * thread may be extending. */
    ld_dl_lock();
    if (idx >= obj->pltrelsz / sizeof(Elf_Reloc))
        ld_die("lazy binding: PLT index out of range");

    const Elf_Reloc *r = &obj->jmprel[idx];
    ld_u32 sym = ELF_R_SYM(r->r_info);
    const char *name = obj->strtab + obj->symtab[sym].st_name;
    int ifunc = 0;
    ld_addr v = ld_reloc_resolve_ifunc(obj, sym, name, &ifunc);
    if (v == 0) {
        /* There is nothing to jump to: the function is being called. */
        undefined_symbol(obj, "symbol", name);
        ld_die("lazy binding failed");
    }
    if (ifunc) v = ld_ifunc_call(v);
    *(ld_addr *)(r->r_offset + obj->base) = v;
    ld_dl_unlock();
    return v;
}
