/*
 * ld_reloc.c — apply DT_REL and DT_JMPREL on a loaded object.
 *
 * Phase 3 supports the i386 relocation types listed below; every
 * other type aborts with a diagnostic so we never silently scribble
 * the wrong value:
 *
 *   R_386_NONE     — no-op.
 *   R_386_RELATIVE — *p += base                (already done in
 *                    ld_start.S for our own image; included here
 *                    for shared libraries the loader brings in).
 *   R_386_GLOB_DAT — *p  = S
 *   R_386_JMP_SLOT — *p  = S  (eager binding; no lazy stub)
 *   R_386_32       — *p  = S + A   (A = current contents of p)
 *   R_386_PC32     — *p  = S + A - P
 *
 * No DT_RELA on i386 by spec.  R_386_COPY copies a DSO's data
 * bytes into a non-PIE executable's own .bss slot; it is applied
 * in a dedicated final pass (ld_relocate_copy) AFTER every object
 * has been through ld_relocate, because the copy reads the source
 * variable's *relocated* value — running it while the providing
 * library is still unrelocated copies zero.
 */

#include "ld.h"

/* If the importer carries DT_VERSYM + DT_VERNEED, find the version
 * hash that this reference requires.  Returns 0 if either the
 * object has no versioning or this particular reference is
 * unversioned (VER_NDX_GLOBAL).  Used to keep cross-DSO links
 * inside the right ABI version family — vital for libstdc++ which
 * legitimately ships multiple definitions of the same symbol name
 * at different GLIBCXX_3.4.* version tags. */
static ld_u32 importer_version_hash(const ld_obj_t *obj, ld_u32 sym_idx) {
    if (!obj->versym || !obj->verneed) return 0;
    Elf32_Half vs = obj->versym[sym_idx];
    ld_u32 ndx = VER_NDX(vs);
    if (ndx == VER_NDX_LOCAL || ndx == VER_NDX_GLOBAL) return 0;

    /* Walk the verneed entries, look up the vernaux whose vna_other
     * matches our VERSYM ndx. */
    unsigned char *p = (unsigned char *)obj->verneed;
    for (ld_u32 i = 0; i < obj->verneednum; i++) {
        Elf32_Verneed *vn = (Elf32_Verneed *)p;
        unsigned char *ap = p + vn->vn_aux;
        for (ld_u32 j = 0; j < vn->vn_cnt; j++) {
            Elf32_Vernaux *va = (Elf32_Vernaux *)ap;
            if (va->vna_other == ndx)
                return va->vna_hash;
            if (va->vna_next == 0) break;
            ap += va->vna_next;
        }
        if (vn->vn_next == 0) break;
        p += vn->vn_next;
    }
    return 0;
}

/* Wrapper: version-aware ld_resolve.  Picks the right resolver
 * depending on whether the importer has versioning metadata. */
static ld_u32 resolve_for(const ld_obj_t *obj, ld_u32 sym_idx,
                          const char *name) {
    ld_u32 vh = importer_version_hash(obj, sym_idx);
    /* Pass the requesting object so resolve_pred won't hand the program
     * its own canonical-PLT entry (function-address equality). */
    return ld_resolve_req(name, vh, obj);
}

static int apply_one(ld_obj_t *obj, Elf32_Rel *r) {
    ld_u32 type = ELF32_R_TYPE(r->r_info);
    ld_u32 sym  = ELF32_R_SYM(r->r_info);
    ld_u32 *p   = (ld_u32 *)(r->r_offset + obj->base);

    switch (type) {
    case R_386_NONE:
        return 0;

    case R_386_RELATIVE:
        /* Per the ABI the addend is the current contents of *p,
         * which for a freshly-mapped image is the link-time vaddr.
         * Adding the base bias gives the runtime address. */
        *p += obj->base;
        return 0;

    case R_386_GLOB_DAT:
    case R_386_JMP_SLOT: {
        if (sym == 0) {
            ld_puts("ld.so: GLOB_DAT/JMP_SLOT with sym=0 in ");
            ld_puts(obj->name); ld_puts("\n");
            return -1;
        }
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        ld_u32 v = resolve_for(obj, sym, name);
        if (v == 0) {
            /* Weak undefined symbols are allowed to remain 0. */
            unsigned char bind = ELF32_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) { *p = 0; return 0; }
            ld_puts("ld.so: undefined symbol: "); ld_puts(name);
            ld_puts(" in "); ld_puts(obj->name); ld_puts("\n");
            return -1;
        }
        *p = v;
        return 0;
    }

    case R_386_32: {
        if (sym == 0) { *p += obj->base; return 0; }
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        ld_u32 v = resolve_for(obj, sym, name);
        if (v == 0) {
            unsigned char bind = ELF32_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) { /* keep addend */ return 0; }
            ld_puts("ld.so: undefined R_386_32: "); ld_puts(name); ld_puts("\n");
            return -1;
        }
        *p = v + *p;            /* S + A; A = current contents */
        return 0;
    }

    case R_386_PC32: {
        if (sym == 0) return 0;
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        ld_u32 v = resolve_for(obj, sym, name);
        if (v == 0) {
            unsigned char bind = ELF32_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) return 0;
            ld_puts("ld.so: undefined R_386_PC32: "); ld_puts(name); ld_puts("\n");
            return -1;
        }
        *p = v + *p - (ld_u32)(unsigned long)p;   /* S + A - P */
        return 0;
    }

    case R_386_COPY: {
        /* Find the source-of-truth in any OTHER loaded object,
         * memcpy the symbol's bytes into our slot.  The size comes
         * from the source symbol's st_size; the executable's
         * version of the symbol must be at least that big (it
         * is — same .o-defined extern data both sides agree on). */
        if (sym == 0) return 0;
        const char *name = obj->strtab + obj->symtab[sym].st_name;
        ld_u32 size = 0;
        ld_u32 src = ld_resolve_with_size(name, obj, &size);
        if (src == 0) {
            unsigned char bind = ELF32_ST_BIND(obj->symtab[sym].st_info);
            if (bind == STB_WEAK) return 0;
            ld_puts("ld.so: R_386_COPY: undefined "); ld_puts(name);
            ld_puts(" in "); ld_puts(obj->name); ld_puts("\n");
            return -1;
        }
        unsigned char *dst = (unsigned char *)p;
        const unsigned char *s = (const unsigned char *)(unsigned long)src;
        for (ld_u32 i = 0; i < size; i++) dst[i] = s[i];
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
        Elf32_Sym *s = &obj->symtab[sym];
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
            *p = val - def->tls_offset + *p;
            return 0;
        }
        if (obj->tls_memsz == 0) {
            ld_puts("ld.so: TPOFF reloc but obj has no PT_TLS: ");
            ld_puts(obj->name); ld_puts("\n");
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

    case R_386_TLS_DTPMOD32:
        /* Module-id half of the tls_index pair consumed by
         * __tls_get_addr in the GD model.  For undef-sym variants
         * (LDM uses sym=0) the module is the current object;
         * otherwise it's the defining object of the symbol.  In
         * either case the binding stays inside this DSO because
         * GD on a globally-defined TLS symbol is unusual — gcc
         * would have emitted IE/TPOFF instead.  Substrate always
         * binds DTPMOD32 to obj->tls_modid for both flavours,
         * which is correct for the LDM/GD-of-local cases that
         * libstdc++ and __thread C++ types produce. */
        *p = obj->tls_modid;
        return 0;

    case R_386_TLS_DTPOFF32: {
        /* Offset-within-module half of the GD/LD pair.  For LDM
         * (sym == 0) the addend already carries the offset.  For
         * GD-with-symbol, resolve to st_value within obj. */
        if (sym == 0) return 0;
        Elf32_Sym *s = &obj->symtab[sym];
        if (s->st_shndx == SHN_UNDEF) {
            ld_puts("ld.so: DTPOFF undef in "); ld_puts(obj->name); ld_puts("\n");
            return -1;
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

int ld_relocate(ld_obj_t *obj) {
    /* R_386_RELATIVE is `*p += base` (non-idempotent).  Apply
     * relocations exactly once per object — re-running on an
     * already-relocated object doubles the bias on relative
     * entries and silently corrupts everything.  dlopen's
     * "relocate the world" pass relies on this guard to be safe. */
    if (obj->relocated) return 0;
    if (obj->rel) {
        ld_u32 n = obj->relsz / sizeof(Elf32_Rel);
        for (ld_u32 i = 0; i < n; i++) {
            /* R_386_COPY is deferred to ld_relocate_copy: it reads
             * the source DSO's *relocated* value, which is not yet
             * available while libraries later in the list are still
             * unrelocated. */
            if (ELF32_R_TYPE(obj->rel[i].r_info) == R_386_COPY) continue;
            if (apply_one(obj, &obj->rel[i]) != 0) return -1;
        }
    }
    if (obj->jmprel) {
        ld_u32 n = obj->pltrelsz / sizeof(Elf32_Rel);
        for (ld_u32 i = 0; i < n; i++) {
            if (apply_one(obj, &obj->jmprel[i]) != 0) return -1;
        }
    }
    obj->relocated = 1;
    return 0;
}

int ld_relocate_copy(ld_obj_t *obj) {
    /* Final pass: only R_386_COPY, only DT_REL (the PLT never
     * carries copy relocs).  Runs once per object after every
     * object has been through ld_relocate(). */
    if (obj->copy_relocated) return 0;
    if (obj->rel) {
        ld_u32 n = obj->relsz / sizeof(Elf32_Rel);
        for (ld_u32 i = 0; i < n; i++) {
            if (ELF32_R_TYPE(obj->rel[i].r_info) != R_386_COPY) continue;
            if (apply_one(obj, &obj->rel[i]) != 0) return -1;
        }
    }
    obj->copy_relocated = 1;
    return 0;
}
