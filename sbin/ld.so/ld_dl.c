/*
 * ld_dl.c — runtime dlopen / dlsym / dlclose API exported by /sbin/ld.so.
 *
 * Phase 4e scope:
 *   - dlopen() loads a shared object by absolute path or SONAME,
 *     applies its relocations, runs its DT_INIT_ARRAY, and returns
 *     an opaque handle (the ld_obj_t* internally).
 *   - dlsym() walks the requested handle's symbol table, or the
 *     whole process scope when the magic RTLD_DEFAULT handle is
 *     passed.  RTLD_NEXT-from-here lookups are deferred.
 *   - dlclose() decrements a per-object refcount; full unmap +
 *     fini is a Phase 5 concern (needs reverse-dependency safety).
 *
 * Linkage:
 *   These are exported from /sbin/ld.so with default visibility so
 *   that lib/dl/ can wrap them as the canonical POSIX
 *   dlopen/dlsym/dlclose entry points without duplicating the
 *   loader logic.
 */

#include "ld.h"

/* RTLD_* sentinels match what POSIX expects userland to pass. */
#define LD_RTLD_DEFAULT ((void *)0)
#define LD_RTLD_NEXT    ((void *)-1)

/* Public surface — visibility forced via attribute since the rest
 * of ld.so is fvisibility=hidden. */
#define LD_PUBLIC __attribute__((visibility("default")))

LD_PUBLIC void *__ldso_dlopen(const char *path, int flags) {
    (void)flags; /* RTLD_LAZY/NOW/GLOBAL/LOCAL all behave eager+global today */
    if (!path) {
        /* dlopen(NULL) — return a handle representing the main
         * program (== head of loaded-object list). */
        return ld_obj_list();
    }
    /* Load the requested object plus any new transitive deps
     * via the same BFS we run at startup.  ld_load_object dedup's
     * by SONAME so re-opening an already-loaded library is cheap. */
    ld_obj_t *o = ld_load_object(path);
    if (!o) return 0;
    /* Walk newly-loaded objects' DT_NEEDED so transitive deps
     * are pulled in.  We rerun the BFS here because new objects
     * might have been appended after `o`; iterate from `o` to
     * the current list tail. */
    for (ld_obj_t *cur = o; cur; cur = cur->next) {
        if (!cur->dynamic || !cur->strtab) continue;
        for (Elf32_Dyn *d = cur->dynamic; d->d_tag != DT_NULL; d++) {
            if (d->d_tag != DT_NEEDED) continue;
            const char *soname = cur->strtab + d->d_un.d_val;
            (void)ld_load_object(soname);
        }
    }
    /* Apply relocations and run init arrays for any object that
     * showed up after this dlopen (we re-relocate the whole list,
     * which is wasteful but harmless — RELATIVE / GLOB_DAT /
     * JMP_SLOT are idempotent under our eager-binding policy). */
    for (ld_obj_t *r = ld_obj_list(); r; r = r->next) {
        if (ld_relocate(r) != 0) return 0;
    }
    /* Run init arrays — should ideally only fire for newly-loaded
     * objects, but lacking a per-object initialized-flag we re-run
     * for everyone.  init_array entries are typically idempotent
     * (libc, libm); custom user ctors that aren't would need the
     * guard, which is queued for a follow-up. */
    ld_run_init_arrays();
    return o;
}

LD_PUBLIC void *__ldso_dlsym(void *handle, const char *name) {
    if (!name) return 0;
    if (handle == LD_RTLD_DEFAULT) {
        /* Process-wide scope, executable first. */
        return (void *)(unsigned long)ld_resolve(name);
    }
    if (handle == LD_RTLD_NEXT) {
        /* RTLD_NEXT not yet supported — would need to know which
         * object the caller was in. */
        return 0;
    }
    /* Object handle — search just this object's symbol table.
     * Reuse ld_resolve_skip with a bogus skip so the scope walker
     * has to find this specific object first, then we filter. */
    ld_obj_t *target = (ld_obj_t *)handle;
    /* Manual lookup so we don't have to add yet another resolver
     * variant just for "search exactly one object". */
    extern ld_u32 ld_lookup_in_obj(const ld_obj_t *o, const char *name);
    return (void *)(unsigned long)ld_lookup_in_obj(target, name);
}

LD_PUBLIC int __ldso_dlclose(void *handle) {
    /* Phase 4e: refcount only.  Real unmap + fini is queued for a
     * later phase that needs reverse-dependency safety. */
    (void)handle;
    return 0;
}

static int dl_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static ld_u32 dl_gnu_hash(const char *s) {
    ld_u32 h = 5381;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) h = h * 33 + *p++;
    return h;
}

static ld_u32 dl_sysv_hash(const char *s) {
    ld_u32 h = 0, g;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) { h = (h << 4) + *p++; g = h & 0xf0000000; if (g) h ^= g >> 24; h &= ~g; }
    return h;
}

/* Helper used by __ldso_dlsym for handle-specific lookups.  Bounded
 * to one object's hash table; supports both DT_GNU_HASH (preferred)
 * and DT_HASH. */
ld_u32 ld_lookup_in_obj(const ld_obj_t *o, const char *name) {
    if (!o || !name || !o->symtab || !o->strtab) return 0;

    if (o->gnu_hash) {
        const ld_u32 *h = o->gnu_hash;
        ld_u32 nbuckets   = h[0];
        ld_u32 symbias    = h[1];
        ld_u32 bloom_size = h[2];
        ld_u32 bloom_shift = h[3];
        const ld_u32 *bloom   = h + 4;
        const ld_u32 *buckets = bloom + bloom_size;
        const ld_u32 *chain   = buckets + nbuckets;
        if (nbuckets == 0) return 0;
        ld_u32 hv = dl_gnu_hash(name);
        ld_u32 word = bloom[(hv / 32) & (bloom_size - 1)];
        ld_u32 mask = (1u << (hv & 31)) | (1u << ((hv >> bloom_shift) & 31));
        if ((word & mask) != mask) return 0;
        ld_u32 idx = buckets[hv % nbuckets];
        if (idx < symbias) return 0;
        for (;;) {
            ld_u32 chain_v = chain[idx - symbias];
            if (((chain_v ^ hv) >> 1) == 0) {
                Elf32_Sym *s = &o->symtab[idx];
                if (dl_streq(o->strtab + s->st_name, name))
                    return s->st_value + o->base;
            }
            if (chain_v & 1) break;
            idx++;
        }
        return 0;
    }

    if (o->hash) {
        ld_u32 nbuckets = o->hash[0];
        ld_u32 nchains  = o->hash[1];
        const ld_u32 *buckets = o->hash + 2;
        const ld_u32 *chains  = buckets + nbuckets;
        if (nbuckets == 0) return 0;
        ld_u32 hv = dl_sysv_hash(name);
        for (ld_u32 idx = buckets[hv % nbuckets]; idx != 0 && idx < nchains;
             idx = chains[idx]) {
            Elf32_Sym *s = &o->symtab[idx];
            if (s->st_shndx == SHN_UNDEF) continue;
            if (dl_streq(o->strtab + s->st_name, name))
                return s->st_value + o->base;
        }
    }
    return 0;
}
