/*
 * ld_reloc.c - apply an object's relocation table and DT_JMPREL.
 *
 * This file holds what the architectures share: walking the tables
 * (DT_REL on i386, DT_RELA on amd64 - Elf_Reloc and LD_DT_REL in ld.h),
 * the version-aware symbol lookup, and the two-pass ordering that defers
 * COPY relocations.  What each relocation type means is the
 * architecture's business: ld_reloc_apply() lives in ld_reloc_i386.c and
 * ld_reloc_amd64.c, and every type it does not know aborts with a
 * diagnostic so we never silently scribble the wrong value.
 *
 * A COPY relocation copies a DSO's data bytes into a non-PIE
 * executable's own .bss slot; it is applied in a dedicated final pass
 * (ld_relocate_copy) AFTER every object has been through ld_relocate,
 * because the copy reads the source variable's *relocated* value -
 * running it while the providing library is still unrelocated copies
 * zero.
 *
 * Indirect functions.  An IRELATIVE relocation, and a GLOB_DAT /
 * JMP_SLOT / absolute relocation whose symbol resolves to an
 * STT_GNU_IFUNC definition, store what a resolver function returns.  The
 * resolver is ordinary code in some object, so it is not called from the
 * main pass, where that object may not be relocated yet: ld_reloc_apply
 * skips the entry and marks the object has_ifunc, and a third pass,
 * ld_relocate_ifunc(), comes back with ifunc_pass set once every object
 * is relocated.  In that pass ld_reloc_apply leaves everything else
 * alone - which on i386, where the addend is the word itself, is what
 * keeps the other entries from being applied twice.
 *
 * Lazy binding.  An object linked without -z now has, in each PLT slot
 * of .got.plt, the link-time address of that slot's stub in .plt; the
 * stub pushes a word naming its DT_JMPREL entry and jumps to PLT0, which
 * pushes GOT[1] and jumps through GOT[2].  ld_reloc_lazy_setup() biases
 * the slots, puts the object in GOT[1] and ld_plt_trampoline in GOT[2],
 * and the first call of each function then arrives in ld_plt_fixup(),
 * which resolves the symbol, stores it in the slot and returns it for
 * the trampoline to jump to.  An undefined function is therefore
 * reported when it is first called, not when the object is loaded.
 * Binding is eager instead when the object asks for it (DT_BIND_NOW,
 * DF_BIND_NOW, DF_1_NOW - which is how the system libraries are built),
 * when LD_BIND_NOW is set, and for what a dlopen(RTLD_NOW) loads.
 */

#include "ld.h"

/* If the importer carries DT_VERSYM + DT_VERNEED, find the version
 * hash that this reference requires.  Returns 0 if either the
 * object has no versioning or this particular reference is
 * unversioned (VER_NDX_GLOBAL).  Used to keep cross-DSO links
 * inside the right ABI version family - vital for libstdc++ which
 * legitimately ships multiple definitions of the same symbol name
 * at different GLIBCXX_3.4.* version tags. */
static ld_u32 importer_version_hash(const ld_obj_t *obj, ld_u32 sym_idx) {
    if (!obj->versym || !obj->verneed) return 0;
    Elf_Half vs = obj->versym[sym_idx];
    ld_u32 ndx = VER_NDX(vs);
    if (ndx == VER_NDX_LOCAL || ndx == VER_NDX_GLOBAL) return 0;

    /* Walk the verneed entries, look up the vernaux whose vna_other
     * matches our VERSYM ndx. */
    unsigned char *p = (unsigned char *)obj->verneed;
    for (ld_u32 i = 0; i < obj->verneednum; i++) {
        Elf_Verneed *vn = (Elf_Verneed *)p;
        unsigned char *ap = p + vn->vn_aux;
        for (ld_u32 j = 0; j < vn->vn_cnt; j++) {
            Elf_Vernaux *va = (Elf_Vernaux *)ap;
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

/* Version-aware ld_resolve.  Picks the right resolver depending on
 * whether the importer has versioning metadata. */
ld_addr ld_reloc_resolve(const ld_obj_t *obj, ld_u32 sym_idx,
                         const char *name) {
    ld_u32 vh = importer_version_hash(obj, sym_idx);
    /* Pass the requesting object so resolve_pred won't hand the program
     * its own canonical-PLT entry (function-address equality). */
    return ld_resolve_req(name, vh, obj);
}

ld_addr ld_reloc_resolve_ifunc(const ld_obj_t *obj, ld_u32 sym_idx,
                               const char *name, int *ifunc_out) {
    ld_u32 vh = importer_version_hash(obj, sym_idx);
    return ld_resolve_req_ifunc(name, vh, obj, ifunc_out);
}

int ld_bind_now;

int ld_reloc_lazy_setup(ld_obj_t *obj) {
    if (ld_bind_now || obj->bind_now || !obj->pltgot) return 0;

    /* Only a table of plain PLT slots, each still holding its stub's
     * address, can be left for later; anything else is bound now. */
    ld_addr n = obj->pltrelsz / sizeof(Elf_Reloc);
    for (ld_addr i = 0; i < n; i++) {
        const Elf_Reloc *r = &obj->jmprel[i];
        ld_u32 type = ELF_R_TYPE(r->r_info);
        if (type == LD_R_IRELATIVE) continue;
        if (type != LD_R_JMP_SLOT) return 0;
        if (*(ld_addr *)(r->r_offset + obj->base) == 0) return 0;
    }

    for (ld_addr i = 0; i < n; i++) {
        const Elf_Reloc *r = &obj->jmprel[i];
        if (ELF_R_TYPE(r->r_info) == LD_R_IRELATIVE) {
            obj->has_ifunc = 1;     /* ld_relocate_ifunc fills the slot */
            continue;
        }
        *(ld_addr *)(r->r_offset + obj->base) += obj->base;
    }
    obj->pltgot[1] = (ld_addr)(unsigned long)obj;
    obj->pltgot[2] = (ld_addr)(unsigned long)ld_plt_trampoline;
    obj->lazy = 1;
    return 1;
}

ld_addr ld_plt_fixup(ld_obj_t *obj, ld_addr arg) {
#ifdef LD_ARCH_AMD64
    ld_addr idx = arg;                      /* the stub pushed an index */
#else
    ld_addr idx = arg / sizeof(Elf_Reloc);  /* ... a byte offset */
#endif
    /* The lookup walks the loaded-object list, which a dlopen in another
     * thread may be extending. */
    ld_dl_lock();
    if (idx >= obj->pltrelsz / sizeof(Elf_Reloc))
        ld_die("lazy binding: PLT entry out of range");

    const Elf_Reloc *r = &obj->jmprel[idx];
    ld_u32 sym = ELF_R_SYM(r->r_info);
    const char *name = obj->strtab + obj->symtab[sym].st_name;
    int ifunc = 0;
    ld_addr v = ld_reloc_resolve_ifunc(obj, sym, name, &ifunc);
    if (v == 0) {
        /* There is nothing to jump to: the function is being called. */
        ld_puts(LD_SELF_NAME ": undefined symbol: "); ld_puts(name);
        ld_puts(" in "); ld_puts(obj->name); ld_puts("\n");
        ld_die("lazy binding failed");
    }
    if (ifunc) v = ld_ifunc_call(v);
    *(ld_addr *)(r->r_offset + obj->base) = v;
    ld_dl_unlock();
    return v;
}

int ld_relocate(ld_obj_t *obj) {
    /* A RELATIVE relocation on i386 is `*p += base` (non-idempotent).
     * Apply relocations exactly once per object - re-running on an
     * already-relocated object doubles the bias on relative
     * entries and silently corrupts everything.  dlopen's
     * "relocate the world" pass relies on this guard to be safe. */
    if (obj->relocated) return 0;
    if (obj->rel) {
        ld_addr n = obj->relsz / sizeof(Elf_Reloc);
        for (ld_addr i = 0; i < n; i++) {
            /* COPY is deferred to ld_relocate_copy: it reads
             * the source DSO's *relocated* value, which is not yet
             * available while libraries later in the list are still
             * unrelocated. */
            if (ELF_R_TYPE(obj->rel[i].r_info) == LD_R_COPY) continue;
            if (ld_reloc_apply(obj, &obj->rel[i]) != 0) return -1;
        }
    }
    /* The PLT slots are bound now unless the architecture can leave them
     * for the first call. */
    if (obj->jmprel && !ld_reloc_lazy_setup(obj)) {
        ld_addr n = obj->pltrelsz / sizeof(Elf_Reloc);
        for (ld_addr i = 0; i < n; i++) {
            if (ld_reloc_apply(obj, &obj->jmprel[i]) != 0) return -1;
        }
    }
    obj->relocated = 1;
    return 0;
}

int ld_relocate_ifunc(ld_obj_t *obj) {
    /* Entries that need a resolver called, which ld_reloc_apply left
     * alone (and noted in has_ifunc) while other objects were still
     * unrelocated.  With ifunc_pass set it applies those and nothing
     * else. */
    if (obj->ifunc_relocated) return 0;
    if (obj->has_ifunc) {
        int rc = 0;
        obj->ifunc_pass = 1;
        if (obj->rel) {
            ld_addr n = obj->relsz / sizeof(Elf_Reloc);
            for (ld_addr i = 0; rc == 0 && i < n; i++)
                rc = ld_reloc_apply(obj, &obj->rel[i]);
        }
        if (obj->jmprel) {
            ld_addr n = obj->pltrelsz / sizeof(Elf_Reloc);
            for (ld_addr i = 0; rc == 0 && i < n; i++)
                rc = ld_reloc_apply(obj, &obj->jmprel[i]);
        }
        obj->ifunc_pass = 0;
        if (rc != 0) return -1;
    }
    obj->ifunc_relocated = 1;
    return 0;
}

int ld_relocate_copy(ld_obj_t *obj) {
    /* Final pass: only COPY, only the main relocation table (the PLT
     * never carries copy relocs).  Runs once per object after every
     * object has been through ld_relocate(). */
    if (obj->copy_relocated) return 0;
    if (obj->rel) {
        ld_addr n = obj->relsz / sizeof(Elf_Reloc);
        for (ld_addr i = 0; i < n; i++) {
            if (ELF_R_TYPE(obj->rel[i].r_info) != LD_R_COPY) continue;
            if (ld_reloc_apply(obj, &obj->rel[i]) != 0) return -1;
        }
    }
    obj->copy_relocated = 1;
    return 0;
}
