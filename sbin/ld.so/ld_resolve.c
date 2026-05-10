/*
 * ld_resolve.c — symbol resolution against the loaded-object list.
 *
 * Phase 3 strategy: walk the loaded-object list in load order; for
 * each object, hash-lookup the name in DT_GNU_HASH (preferred) or
 * fall back to DT_HASH.  Return the first non-undef definition.
 *
 * Skipped for now (deferred to later phases):
 *   - Symbol versioning (DT_VERSYM / DT_VERDEF / DT_VERNEED).
 *   - LD_PRELOAD / interposition rules.
 *   - Weak symbol semantics — treat WEAK undef as "keep looking",
 *     but don't promote weak defs over strong defs in later objects
 *     (we always take the first definition seen).
 *   - STV_HIDDEN / STV_PROTECTED visibility filtering — every
 *     globally-bound symbol is currently eligible.
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
 * On i386 size_t is 4 bytes so bloom words are 32-bit. */
static Elf32_Sym *lookup_gnu(const ld_obj_t *o, const char *name) {
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
            if (strcmp_local(sname, name) == 0) return s;
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
 *                             0 terminates) */
static Elf32_Sym *lookup_sysv(const ld_obj_t *o, const char *name) {
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
        if (strcmp_local(sname, name) == 0) return s;
    }
    return 0;
}

/* Internal: scope walk with optional skip-this-object and a place
 * to deposit the symbol's st_size for R_386_COPY callers. */
static ld_u32 resolve_internal(const char *name, const ld_obj_t *skip,
                               ld_u32 *size_out) {
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o == skip) continue;
        Elf32_Sym *s = lookup_gnu(o, name);
        if (!s) s = lookup_sysv(o, name);
        if (!s) continue;
        if (s->st_shndx == SHN_UNDEF) continue;
        unsigned char bind = ELF32_ST_BIND(s->st_info);
        if (bind != STB_GLOBAL && bind != STB_WEAK) continue;
        if (size_out) *size_out = s->st_size;
        return s->st_value + o->base;
    }
    if (size_out) *size_out = 0;
    return 0;
}

ld_u32 ld_resolve(const char *name) {
    return resolve_internal(name, 0, 0);
}

ld_u32 ld_resolve_skip(const char *name, const ld_obj_t *skip) {
    return resolve_internal(name, skip, 0);
}

ld_u32 ld_resolve_with_size(const char *name, const ld_obj_t *skip,
                            ld_u32 *size_out) {
    return resolve_internal(name, skip, size_out);
}
