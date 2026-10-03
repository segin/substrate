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
    if (obj->jmprel) {
        ld_addr n = obj->pltrelsz / sizeof(Elf_Reloc);
        for (ld_addr i = 0; i < n; i++) {
            if (ld_reloc_apply(obj, &obj->jmprel[i]) != 0) return -1;
        }
    }
    obj->relocated = 1;
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
