/*
 * ld_reloc_i386.c - the i386 relocation types (REL: the addend is the
 * word being relocated).
 *
 *   R_386_NONE     - no-op.
 *   R_386_RELATIVE - *p += base                (already done in
 *                    ld_start.S for our own image; included here
 *                    for shared libraries the loader brings in).
 *   R_386_GLOB_DAT - *p  = S
 *   R_386_JMP_SLOT - *p  = S  (bound on first call when the object
 *                    allows it)
 *   R_386_32       - *p  = S + A   (A = current contents of p)
 *   R_386_PC32     - *p  = S + A - P
 *   R_386_COPY     - copy the symbol's bytes from the providing DSO
 *   R_386_TLS_TPOFF, R_386_TLS_DTPMOD32, R_386_TLS_DTPOFF32
 *   R_386_IRELATIVE - *p = the value returned by the resolver at B + A
 *
 * No DT_RELA on i386 by spec.  IRELATIVE, and a GLOB_DAT / JMP_SLOT / 32
 * whose symbol resolves to an indirect function, are deferred to the
 * indirect-function pass.  See ld_reloc.c for that, for the table walk
 * and the ordering of the passes, and for lazy binding.
 */

#include "ld.h"

int ld_reloc_apply(ld_obj_t *obj, Elf_Reloc *r) {
    ld_u32 type = ELF_R_TYPE(r->r_info);
    ld_u32 sym  = ELF_R_SYM(r->r_info);
    ld_u32 *p   = (ld_u32 *)(r->r_offset + obj->base);

    /* The indirect-function pass revisits only what the main pass
     * deferred to it.  Everything else has been applied, and with the
     * addend in the word itself must not be applied again. */
    if (obj->ifunc_pass) {
        switch (type) {
        case R_386_IRELATIVE:
            break;
        case R_386_GLOB_DAT:
        case R_386_32:
            if (sym == 0) return 0;
            break;
        case R_386_JMP_SLOT:
            /* Lazily bound slots stay with ld_plt_fixup. */
            if (sym == 0 || obj->lazy) return 0;
            break;
        default:
            return 0;
        }
    }

    switch (type) {
    case R_386_NONE:
        return 0;

    case R_386_RELATIVE:
        /* Per the ABI the addend is the current contents of *p,
         * which for a freshly-mapped image is the link-time vaddr.
         * Adding the base bias gives the runtime address. */
        *p += obj->base;
        return 0;

    case R_386_IRELATIVE:
        /* The word holds the resolver's link-time address until the
         * indirect-function pass replaces it with what that returns. */
        if (!obj->ifunc_pass) { obj->has_ifunc = 1; return 0; }
        *p = ld_ifunc_call(obj->base + *p);
        return 0;

    case R_386_GLOB_DAT:
    case R_386_JMP_SLOT: {
        if (sym == 0) {
            ld_puts("ld.so: GLOB_DAT/JMP_SLOT with sym=0 in ");
            ld_puts(obj->name); ld_puts("\n");
            return -1;
        }
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        int ifunc = 0;
        ld_u32 v = ld_reloc_resolve_ifunc(obj, sym, name, &ifunc);
        if (v == 0) {
            /* Weak undefined symbols are allowed to remain 0. */
            unsigned char bind = ELF_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) { *p = 0; return 0; }
            ld_puts("ld.so: undefined symbol: "); ld_puts(name);
            ld_puts(" in "); ld_puts(obj->name); ld_puts("\n");
            return -1;
        }
        if (ifunc) {
            if (!obj->ifunc_pass) { obj->has_ifunc = 1; return 0; }
            v = ld_ifunc_call(v);
        } else if (obj->ifunc_pass) {
            return 0;               /* applied by the main pass */
        }
        *p = v;
        return 0;
    }

    case R_386_32: {
        if (sym == 0) { *p += obj->base; return 0; }
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        int ifunc = 0;
        ld_u32 v = ld_reloc_resolve_ifunc(obj, sym, name, &ifunc);
        if (v == 0) {
            unsigned char bind = ELF_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) { /* keep addend */ return 0; }
            ld_puts("ld.so: undefined R_386_32: "); ld_puts(name); ld_puts("\n");
            return -1;
        }
        if (ifunc) {
            if (!obj->ifunc_pass) { obj->has_ifunc = 1; return 0; }
            v = ld_ifunc_call(v);
        } else if (obj->ifunc_pass) {
            return 0;               /* applied by the main pass */
        }
        *p = v + *p;            /* S + A; A = current contents */
        return 0;
    }

    case R_386_PC32: {
        if (sym == 0) return 0;
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        ld_u32 v = ld_reloc_resolve(obj, sym, name);
        if (v == 0) {
            unsigned char bind = ELF_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) return 0;
            ld_puts("ld.so: undefined R_386_PC32: "); ld_puts(name); ld_puts("\n");
            return -1;
        }
        *p = v + *p - (ld_u32)(unsigned long)p;   /* S + A - P */
        return 0;
    }

    case R_386_COPY: {
        /* Find the source-of-truth in any OTHER loaded object and
         * memcpy the symbol's bytes into our slot. */
        if (sym == 0) return 0;
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        ld_u32 size = 0;
        ld_u32 src = ld_resolve_with_size(name, obj, &size);
        if (src == 0) {
            unsigned char bind = ELF_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) return 0;
            ld_puts("ld.so: R_386_COPY: undefined "); ld_puts(name);
            ld_puts(" in "); ld_puts(obj->name); ld_puts("\n");
            return -1;
        }
        /* Copy at most the destination's own size.  Normally both
         * sides agree (same extern data), but a version-skewed
         * provider whose symbol grew would otherwise overrun the
         * executable's .bss slot; clamp to the smaller and warn. */
        ld_u32 dstsize = obj->symtab[sym].st_size;
        ld_u32 n = size;
        if (dstsize && dstsize < n) {
            ld_puts("ld.so: R_386_COPY size mismatch for "); ld_puts(name);
            ld_puts(" - clamping to destination\n");
            n = dstsize;
        }
        unsigned char *dst = (unsigned char *)p;
        const unsigned char *s = (const unsigned char *)(unsigned long)src;
        for (ld_u32 i = 0; i < n; i++) dst[i] = s[i];
        return 0;
    }

    case R_386_TLS_TPOFF: {
        /* Resolve symbol's offset within its module's TLS image,
         * subtract the module's negative-offset-from-TP, store the
         * result so `mov %gs:p, %eax` produces the slot address.
         *
         * sym == 0 is a LOCAL TLS variable (initial-exec of a `static
         * __thread` in a DSO, e.g. libpthread's TSD key vector): the
         * offset within the module's TLS image lives in the addend (*p)
         * and the null symbol's st_value is 0, so the same formula below
         * computes (addend - tls_offset).  Only flag a genuinely
         * undefined NAMED symbol. */
        Elf_Sym *s = &obj->symtab[sym];
        if (sym != 0 && s->st_shndx == SHN_UNDEF) {
            /* Imported TLS symbol (initial-exec referencing a __thread var
             * defined in another module, e.g. libstdc++'s
             * std::__once_call).  Resolve it to its defining module and
             * apply THAT module's tls_offset. */
            const char *nm = obj->strtab + s->st_name;
            const ld_obj_t *def = 0;
            ld_u32 val = ld_resolve_tls(nm, obj, &def);
            if (!def || def->tls_memsz == 0) {
                ld_puts("ld.so: TLS undef sym in "); ld_puts(obj->name);
                ld_puts(": "); ld_puts(nm); ld_puts("\n");
                return -1;
            }
            /* A defining module with no tls_modid was loaded AFTER the
             * one-shot startup TLS layout (dlopen) - its tls_offset is
             * 0, and 0-offset math would produce POSITIVE %gs offsets
             * that scribble over the TCB/DTV.  Fail the relocation
             * (and thus the dlopen) instead of corrupting memory. */
            if (def->tls_modid == 0) {
                ld_puts("ld.so: TLS module loaded after startup (dlopen): ");
                ld_puts(def->name); ld_puts(" - unsupported\n");
                return -1;
            }
            *p = val - def->tls_offset + *p;
            return 0;
        }
        if (obj->tls_memsz == 0) {
            ld_puts("ld.so: TPOFF reloc but obj has no PT_TLS: ");
            ld_puts(obj->name); ld_puts("\n");
            return -1;
        }
        /* Same post-startup guard for the local/defined case: an object
         * dlopen'd after ld_setup_tls() has tls_offset 0 - the formula
         * below would emit positive %gs offsets into the TCB/DTV. */
        if (obj->tls_modid == 0) {
            ld_puts("ld.so: TLS module loaded after startup (dlopen): ");
            ld_puts(obj->name); ld_puts(" - unsupported\n");
            return -1;
        }
        /* Per the i386 TLS variant-II ABI: TPOFF = sym_value - tls_offset
         * where tls_offset is the module's offset below TP.  Since
         * gs:0 sits at TP, gs:NEG produces an address below it.
         * Our `tls_offset` is the absolute (positive) value, so the
         * actual TPOFF is sym_value - obj->tls_offset, which fits in
         * a signed 32-bit; userspace then loads gs:TPOFF. */
        *p = s->st_value - obj->tls_offset + *p;
        return 0;
    }

    case R_386_TLS_DTPMOD32: {
        /* Module-id half of the tls_index pair consumed by
         * __tls_get_addr in the GD model.  LDM (sym == 0) and a
         * GD reference to a TLS symbol DEFINED in this object both
         * bind to obj's own module id.  But a GD reference to a
         * __thread variable defined in ANOTHER DSO (sym UNDEF) must
         * bind to the DEFINING module's id - binding the importer's
         * id made __tls_get_addr return the wrong module's block. */
        ld_u32 modid = obj->tls_modid;
        if (sym != 0 && obj->symtab[sym].st_shndx == SHN_UNDEF) {
            const char *nm = obj->strtab + obj->symtab[sym].st_name;
            const ld_obj_t *def = 0;
            (void)ld_resolve_tls(nm, obj, &def);
            if (!def) {
                ld_puts("ld.so: DTPMOD32 undef TLS sym in ");
                ld_puts(obj->name); ld_puts(": "); ld_puts(nm); ld_puts("\n");
                return -1;
            }
            modid = def->tls_modid;
        }
        if (modid == 0) {
            /* The defining module was loaded after the startup TLS
             * layout (dlopen) - modid 0 makes __tls_get_addr return
             * NULL.  Fail cleanly. */
            ld_puts("ld.so: TLS module loaded after startup (dlopen): ");
            ld_puts(obj->name); ld_puts(" - unsupported\n");
            return -1;
        }
        *p = modid;
        return 0;
    }

    case R_386_TLS_DTPOFF32: {
        /* Offset-within-module half of the GD/LD pair.  For LDM
         * (sym == 0) the addend already carries the offset.  For a
         * GD symbol defined here, use its st_value; for one imported
         * from another DSO, use its st_value within that module
         * (ld_resolve_tls returns the raw, unbiased value). */
        if (sym == 0) return 0;
        Elf_Sym *s = &obj->symtab[sym];
        if (s->st_shndx == SHN_UNDEF) {
            const char *nm = obj->strtab + s->st_name;
            const ld_obj_t *def = 0;
            ld_u32 val = ld_resolve_tls(nm, obj, &def);
            if (!def) {
                ld_puts("ld.so: DTPOFF undef in "); ld_puts(obj->name);
                ld_puts(": "); ld_puts(nm); ld_puts("\n");
                return -1;
            }
            *p = val + *p;
            return 0;
        }
        *p = s->st_value + *p;
        return 0;
    }

    default:
        ld_puts("ld.so: unsupported reloc type "); ld_putd(type);
        ld_puts(" in "); ld_puts(obj->name); ld_puts("\n");
        return -1;
    }
}
