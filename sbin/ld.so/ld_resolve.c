/*
 * ld_resolve.c — symbol resolution against the loaded-object list.
 *
 * Phase 3 + Phase 5 strategy: walk the loaded-object list in load
 * order; for each object, hash-lookup the name in DT_GNU_HASH
 * (preferred) or fall back to DT_HASH.  Return the first
 * non-undef definition that matches the importer's version
 * requirement (if any).
 *
 * Phase 5 adds GNU symbol versioning (DT_VERSYM / DT_VERDEF /
 * DT_VERNEED).  Required by libstdc++.so.6 which decorates every
 * exported symbol with GLIBCXX_3.4.* version tags and ABI-versions
 * a handful of them across releases.  Without version-aware
 * resolution the loader would happily bind a caller's
 * `std::string::compare@GLIBCXX_3.4.21` reference to the unversioned
 * symbol of the same name in a stale libstdc++, silently corrupting
 * the C++ ABI.
 *
 * Still deferred:
 *   - LD_PRELOAD / interposition rules.
 *   - STV_HIDDEN / STV_PROTECTED visibility filtering — every
 *     globally-bound symbol is currently eligible.
 *
 * Weak symbol semantics: WEAK undef references keep looking past
 * undef; first non-undef def (strong or weak) wins.  Standard
 * vague-linkage handling — multiple DSOs may legitimately export
 * the same symbol (vtables, typeinfo, template instantiations) and
 * the runtime picks one canonical instance.
 */

#include "ld.h"

/* GNU hash function — single-pass DJB-style.  Identical formula
 * to glibc and BSD rtld so symbol indices match.  */
static ld_u32 gnu_hash(const char *s) {
    ld_u32 h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

/* SysV hash — used when the .so was built without DT_GNU_HASH. */
static ld_u32 sysv_hash(const char *s) {
    ld_u32 h = 0, g;
    while (*s) {
        h = (h << 4) + (unsigned char)*s++;
        g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

static int strcmp_local(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* GNU-hash table layout (binutils gnu-hash.txt):
 *   uint32 nbuckets
 *   uint32 symbias       — symbol index of first hashed symbol
 *   uint32 bloom_size    — number of bloom words (size_t-wide)
 *   uint32 bloom_shift
 *   size_t bloom[bloom_size]
 *   uint32 buckets[nbuckets]
 *   uint32 chain[nsymbols - symbias]
 *
 * On i386 size_t is 4 bytes so bloom words are 32-bit.
 *
 * Returns the first chain entry whose name matches `name` AND whose
 * st_shndx + binding + version pass `pred(o, sym_idx, pred_arg)`.
 * The chain may contain MULTIPLE entries with the same name but
 * different version indices (libstdc++ does this routinely for
 * GLIBCXX_3.4.* compat shims) so we can't return early on the first
 * name match. */
typedef int (*sym_pred_t)(const ld_obj_t *o, ld_u32 sym_idx, void *arg);

static Elf32_Sym *lookup_gnu(const ld_obj_t *o, const char *name,
                             sym_pred_t pred, void *pred_arg) {
    if (!o->gnu_hash || !o->symtab || !o->strtab) return 0;

    const ld_u32 *h = o->gnu_hash;
    ld_u32 nbuckets   = h[0];
    ld_u32 symbias    = h[1];
    ld_u32 bloom_size = h[2];
    ld_u32 bloom_shift = h[3];
    const ld_u32 *bloom = h + 4;
    const ld_u32 *buckets = bloom + bloom_size;
    const ld_u32 *chain   = buckets + nbuckets;

    if (nbuckets == 0) return 0;

    ld_u32 hv = gnu_hash(name);

    /* Bloom filter: if the (hv % 32) bit AND the ((hv >> shift) % 32)
     * bit aren't both set in the chosen word, the symbol is absent. */
    ld_u32 word = bloom[(hv / 32) & (bloom_size - 1)];
    ld_u32 mask = (1u << (hv & 31)) | (1u << ((hv >> bloom_shift) & 31));
    if ((word & mask) != mask) return 0;

    ld_u32 idx = buckets[hv % nbuckets];
    if (idx < symbias) return 0;
    for (;;) {
        ld_u32 chain_v = chain[idx - symbias];
        if (((chain_v ^ hv) >> 1) == 0) {
            Elf32_Sym *s = &o->symtab[idx];
            const char *sname = o->strtab + s->st_name;
            if (strcmp_local(sname, name) == 0 && pred(o, idx, pred_arg))
                return s;
        }
        if (chain_v & 1) break;     /* end-of-chain marker */
        idx++;
    }
    return 0;
}

/* SysV-hash fallback layout:
 *   uint32 nbuckets
 *   uint32 nchains
 *   uint32 buckets[nbuckets]
 *   uint32 chains[nchains]   (chain[i] = next idx in collision list,
 *                             0 terminates)
 *
 * Same multi-version handling as lookup_gnu: walk past name matches
 * that fail the predicate. */
static Elf32_Sym *lookup_sysv(const ld_obj_t *o, const char *name,
                              sym_pred_t pred, void *pred_arg) {
    if (!o->hash || !o->symtab || !o->strtab) return 0;
    ld_u32 nbuckets = o->hash[0];
    /* nchains = o->hash[1]; */
    const ld_u32 *buckets = o->hash + 2;
    const ld_u32 *chains  = buckets + nbuckets;
    if (nbuckets == 0) return 0;

    ld_u32 hv = sysv_hash(name);
    for (ld_u32 idx = buckets[hv % nbuckets]; idx != 0; idx = chains[idx]) {
        Elf32_Sym *s = &o->symtab[idx];
        if (s->st_shndx == SHN_UNDEF) continue;
        const char *sname = o->strtab + s->st_name;
        if (strcmp_local(sname, name) == 0 && pred(o, idx, pred_arg))
            return s;
    }
    return 0;
}

/* Standard ELF hash — the same function ELF uses for symbol-name
 * hashing in the SysV DT_HASH section, and (separately) for
 * GNU-versioning verdef/verneed name hashes. */
ld_u32 ld_elf_hash(const char *s) {
    ld_u32 h = 0, g;
    while (*s) {
        h = (h << 4) + (unsigned char)*s++;
        g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

/* Given a symbol index in object `o`, decide whether the symbol is
 * eligible to satisfy a lookup requesting version-hash `want_hash`.
 *
 *   want_hash == 0   The caller didn't specify a version (legacy
 *                    unversioned reference, or the importer's
 *                    VERSYM says GLOBAL=1).  Match any symbol whose
 *                    VERSYM is GLOBAL, BASE (vd_flags & VER_FLG_BASE),
 *                    or non-hidden.  Skip hidden non-default
 *                    versions.
 *
 *   want_hash != 0   Caller wants a specific version.  Match iff
 *                    the symbol's verdef carries that vd_hash.
 *
 * For DSOs without DT_VERSYM (substrate's own libc / libm / etc.
 * pre-Phase-5) every symbol matches — versioning is opt-in per
 * object. */
static int version_matches(const ld_obj_t *o, ld_u32 sym_index,
                           ld_u32 want_hash) {
    if (!o->versym || !o->verdef) return 1;   /* unversioned exporter */
    Elf32_Half vs = o->versym[sym_index];
    ld_u32 ndx = VER_NDX(vs);
    int hidden = VER_IS_HIDDEN(vs);

    if (ndx == VER_NDX_LOCAL) return 0;       /* not exported */

    if (want_hash == 0) {
        /* Unversioned reference.  Hidden non-default versions are
         * NOT eligible; only the base/default version answers. */
        if (ndx == VER_NDX_GLOBAL) return 1;
        if (hidden) return 0;
        /* Look up vd_flags for this index. */
        unsigned char *p = (unsigned char *)o->verdef;
        for (ld_u32 i = 0; i < o->verdefnum; i++) {
            Elf32_Verdef *vd = (Elf32_Verdef *)p;
            if (vd->vd_ndx == ndx)
                return (vd->vd_flags & VER_FLG_BASE) != 0
                    || (vd->vd_flags & VER_FLG_WEAK) != 0
                    || !hidden;
            if (vd->vd_next == 0) break;
            p += vd->vd_next;
        }
        return !hidden;
    }

    /* Versioned reference.  Find this index's vd_hash; compare. */
    if (ndx == VER_NDX_GLOBAL) return 0;      /* unversioned def can't
                                                 satisfy versioned req */
    unsigned char *p = (unsigned char *)o->verdef;
    for (ld_u32 i = 0; i < o->verdefnum; i++) {
        Elf32_Verdef *vd = (Elf32_Verdef *)p;
        if (vd->vd_ndx == ndx)
            return vd->vd_hash == want_hash;
        if (vd->vd_next == 0) break;
        p += vd->vd_next;
    }
    return 0;
}

/* Predicate threaded through lookup_gnu / lookup_sysv so a single
 * hash-chain walk can skip past name-matches that fail binding /
 * version checks.  Without this, libstdc++.so.6 self-references like
 *   _ZNSt13basic_istreamIwSt11char_traitsIwEE6ignoreEl
 * fail to resolve: the symbol has two definitions in libstdc++ (one
 * tagged @GLIBCXX_3.4 for ABI compat, one tagged @@GLIBCXX_3.4.5 as
 * the default) and whichever one happens to land first in the chain
 * is what the old single-shot lookup returned.  If that first hit is
 * the hidden compat tag, version_matches rejects it for an
 * unversioned reference and resolve_internal moves on to the next
 * DSO — missing the default-version definition sitting one chain
 * link later. */
static int resolve_pred(const ld_obj_t *o, ld_u32 sym_idx, void *arg) {
    ld_u32 want = *(ld_u32 *)arg;
    Elf32_Sym *s = &o->symtab[sym_idx];
    if (s->st_shndx == SHN_UNDEF) return 0;
    unsigned char bind = ELF32_ST_BIND(s->st_info);
    if (bind != STB_GLOBAL && bind != STB_WEAK) return 0;
    return version_matches(o, sym_idx, want);
}

/* Internal: scope walk with optional skip-this-object, version
 * filter, and a place to deposit the symbol's st_size for R_386_COPY
 * callers. */
static ld_u32 resolve_internal(const char *name, ld_u32 want_ver_hash,
                               const ld_obj_t *skip, ld_u32 *size_out) {
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o == skip) continue;
        Elf32_Sym *s = lookup_gnu(o, name, resolve_pred, &want_ver_hash);
        if (!s) s = lookup_sysv(o, name, resolve_pred, &want_ver_hash);
        if (!s) continue;
        if (size_out) *size_out = s->st_size;
        return s->st_value + o->base;
    }
    if (size_out) *size_out = 0;
    return 0;
}

ld_u32 ld_resolve(const char *name) {
    return resolve_internal(name, 0, 0, 0);
}

ld_u32 ld_resolve_skip(const char *name, const ld_obj_t *skip) {
    return resolve_internal(name, 0, skip, 0);
}

ld_u32 ld_resolve_with_size(const char *name, const ld_obj_t *skip,
                            ld_u32 *size_out) {
    return resolve_internal(name, 0, skip, size_out);
}

ld_u32 ld_resolve_versioned(const char *name, ld_u32 vh_hash,
                            const ld_obj_t *skip, ld_u32 *size_out) {
    return resolve_internal(name, vh_hash, skip, size_out);
}
