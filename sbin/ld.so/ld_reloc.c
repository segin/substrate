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
 * No DT_RELA on i386 by spec.  R_386_COPY is rejected (a Phase-4
 * concern; needed only for executables that copy DSO data, and
 * none of our tests trigger it).
 */

#include "ld.h"

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
        ld_u32 v = ld_resolve(name);
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
        ld_u32 v = ld_resolve(name);
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
        ld_u32 v = ld_resolve(name);
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
         * result so `mov %gs:p, %eax` produces the slot address. */
        Elf32_Sym *s = &obj->symtab[sym];
        if (s->st_shndx == SHN_UNDEF) {
            ld_puts("ld.so: TLS undef sym in "); ld_puts(obj->name); ld_puts("\n");
            return -1;
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
