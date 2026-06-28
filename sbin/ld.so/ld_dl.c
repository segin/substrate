/*
 * ld_dl.c — runtime dlopen / dlsym / dlclose / dlerror / dladdr
 * API exported by /sbin/ld.so to libdl.so.0.
 *
 * Public entries (default visibility, all under __ldso_ prefix to
 * make their loader-internal origin obvious in nm output):
 *
 *   __ldso_dlopen(path, flags)         -> handle / NULL on error
 *   __ldso_dlsym (handle, name, caller_pc) -> address / 0 on error
 *   __ldso_dlclose(handle)             -> 0 on success, -1 on error
 *   __ldso_dlerror()                   -> latest error string, NULL
 *                                         when consumed (POSIX: dlerror
 *                                         returns NULL on the second
 *                                         consecutive call)
 *   __ldso_dladdr(addr, Dl_info_out)   -> non-zero on success
 *
 * Error state: a single global error slot (`g_err`).  Set by any
 * failing operation, cleared by __ldso_dlerror's read.  This matches
 * glibc's per-process behaviour; per-thread error state is deferred.
 *
 * caller_pc for __ldso_dlsym: libdl.so.0's dlsym() wrapper captures
 * __builtin_return_address(0) and forwards it here.  Used to honour
 * RTLD_NEXT (resolve in scope starting AFTER the caller's DSO).
 */

#include "ld.h"

/* RTLD_* sentinels match what POSIX expects userland to pass. */
#define LD_RTLD_DEFAULT ((void *)0)
#define LD_RTLD_NEXT    ((void *)-1)

#define LD_PUBLIC __attribute__((visibility("default")))

/* -------------------------------------------------------------------- *
 * Error string slot
 * -------------------------------------------------------------------- */

static char  g_err[256];
static int   g_err_pending = 0;

static ld_size dl_strlen(const char *s) {
    ld_size n = 0;
    while (s[n]) n++;
    return n;
}

/* Concatenate up to four strings into g_err.  Bounded; truncates
 * silently rather than overflowing. */
static void ld_dl_error(const char *a, const char *b, const char *c, const char *d) {
    g_err[0] = '\0';
    ld_size cap = sizeof(g_err);
    ld_size off = 0;
    const char *parts[4] = { a, b, c, d };
    for (int i = 0; i < 4; i++) {
        if (!parts[i]) continue;
        ld_size n = dl_strlen(parts[i]);
        if (off + n + 1 > cap) n = cap - off - 1;
        for (ld_size k = 0; k < n; k++) g_err[off + k] = parts[i][k];
        off += n;
    }
    g_err[off < cap ? off : cap - 1] = '\0';
    g_err_pending = 1;
}

LD_PUBLIC const char *__ldso_dlerror(void) {
    if (!g_err_pending) return 0;
    g_err_pending = 0;
    return g_err;
}

/* -------------------------------------------------------------------- *
 * dlopen
 * -------------------------------------------------------------------- */

LD_PUBLIC void *__ldso_dlopen(const char *path, int flags) {
    (void)flags; /* RTLD_LAZY/NOW/GLOBAL/LOCAL all behave eager+global today */
    if (!path) {
        /* dlopen(NULL) — return a handle representing the main
         * program (== head of loaded-object list). */
        return ld_obj_list();
    }
    ld_obj_t *o = ld_load_object(path);
    if (!o) {
        ld_dl_error("dlopen(\"", path, "\"): load failed", 0);
        return 0;
    }
    /* Walk newly-loaded objects' DT_NEEDED so transitive deps
     * are pulled in. */
    for (ld_obj_t *cur = o; cur; cur = cur->next) {
        if (!cur->dynamic || !cur->strtab) continue;
        for (Elf32_Dyn *d = cur->dynamic; d->d_tag != DT_NULL; d++) {
            if (d->d_tag != DT_NEEDED) continue;
            const char *soname = cur->strtab + d->d_un.d_val;
            (void)ld_load_object(soname);
        }
    }
    for (ld_obj_t *r = ld_obj_list(); r; r = r->next) {
        if (ld_relocate(r) != 0) {
            ld_dl_error("dlopen(\"", path, "\"): relocation failed", 0);
            return 0;
        }
    }
    ld_run_init_arrays();
    return o;
}

/* -------------------------------------------------------------------- *
 * dlsym
 * -------------------------------------------------------------------- */

/* Find the DSO that owns the given address.  NULL if address is
 * outside every loaded object's PT_LOAD span. */
static ld_obj_t *dl_obj_for_addr(ld_u32 addr) {
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o->load_start == 0 && o->load_end == 0) continue;
        if (addr >= o->load_start && addr < o->load_end) return o;
    }
    return 0;
}

extern ld_u32 ld_lookup_in_obj(const ld_obj_t *o, const char *name);

LD_PUBLIC void *__ldso_dlsym(void *handle, const char *name, void *caller_pc) {
    if (!name) { ld_dl_error("dlsym: null name", 0, 0, 0); return 0; }

    if (handle == LD_RTLD_DEFAULT) {
        ld_u32 v = ld_resolve(name);
        if (!v) ld_dl_error("dlsym(\"", name, "\"): not found", 0);
        return (void *)(unsigned long)v;
    }

    if (handle == LD_RTLD_NEXT) {
        /* Resolve in the global scope, but skip the caller's DSO
         * and everything before it.  POSIX: "the search begins
         * with the object AFTER the one that called dlsym." */
        if (!caller_pc) {
            ld_dl_error("dlsym(RTLD_NEXT): no caller PC", 0, 0, 0);
            return 0;
        }
        ld_obj_t *caller = dl_obj_for_addr((ld_u32)(unsigned long)caller_pc);
        if (!caller) {
            ld_dl_error("dlsym(RTLD_NEXT): caller not in any DSO", 0, 0, 0);
            return 0;
        }
        /* Walk objects in load order; skip until we've passed
         * caller, then look in each subsequent one. */
        int past_caller = 0;
        for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
            if (!past_caller) {
                if (o == caller) past_caller = 1;
                continue;
            }
            ld_u32 v = ld_lookup_in_obj(o, name);
            if (v) return (void *)(unsigned long)v;
        }
        ld_dl_error("dlsym(RTLD_NEXT, \"", name, "\"): not found", 0);
        return 0;
    }

    /* Object handle — search just this object's symbol table. */
    ld_obj_t *target = (ld_obj_t *)handle;
    ld_u32 v = ld_lookup_in_obj(target, name);
    if (!v) ld_dl_error("dlsym(\"", name, "\"): not found in handle", 0);
    return (void *)(unsigned long)v;
}

/* -------------------------------------------------------------------- *
 * dlclose — run fini, decrement (notional) refcount, do not unmap.
 *
 * Real unmap requires reverse-dependency tracking (every symbol that
 * was resolved through this object's hash must still be reachable
 * elsewhere or never called) — Phase 5+ concern.  For now dlclose
 * delivers its most useful behaviour: runs DT_FINI / DT_FINI_ARRAY
 * for the handle so global destructors fire.
 * -------------------------------------------------------------------- */

LD_PUBLIC int __ldso_dlclose(void *handle) {
    if (!handle || handle == LD_RTLD_DEFAULT || handle == LD_RTLD_NEXT) {
        ld_dl_error("dlclose: invalid handle", 0, 0, 0);
        return -1;
    }
    ld_obj_t *o = (ld_obj_t *)handle;
    if (o->finalized) return 0;
    /* Run DT_FINI_ARRAY in REVERSE registration order, then DT_FINI
     * — matches the order glibc uses. */
    if (o->fini_array && o->fini_arraysz >= sizeof(void (*)(void))) {
        ld_u32 cnt = o->fini_arraysz / sizeof(void (*)(void));
        for (ld_u32 j = cnt; j > 0; j--) {
            if (o->fini_array[j - 1]) o->fini_array[j - 1]();
        }
    }
    if (o->fini) o->fini();
    o->finalized = 1;
    return 0;
}

/* -------------------------------------------------------------------- *
 * dladdr — given an address, find the DSO + symbol containing it.
 * -------------------------------------------------------------------- */

/* Walk a DSO's dynamic symbol table for the symbol whose
 * [st_value, st_value + st_size) range contains `target`.  Skip
 * undefined and zero-size symbols.  Returns 0 if no match. */
static Elf32_Sym *dl_find_sym_in_obj(const ld_obj_t *o, ld_u32 target) {
    if (!o->symtab) return 0;
    /* Determine symtab range from the hash table — DT_HASH has
     * the symbol count in chain[]; DT_GNU_HASH requires walking
     * the chain.  Cheaper: walk via the strtab end, since strtab
     * follows symtab in the typical link layout and we have strsz.
     * Fall back to "until we run off the end of strtab" which
     * bounds it safely. */
    ld_size max_syms = 65536;   /* sanity cap — no DSO has more in practice */
    Elf32_Sym *best = 0;
    ld_u32 best_off = 0xFFFFFFFFu;
    for (ld_size i = 0; i < max_syms; i++) {
        Elf32_Sym *s = &o->symtab[i];
        if (s->st_name == 0 && s->st_value == 0 && s->st_size == 0) {
            /* Could be the index-0 sentinel OR run-off-end —
             * after the sentinel we keep going, but if we see
             * many zeros in a row we bail. */
            if (i > 0) {
                /* Heuristic: 8 zero entries in a row means we've
                 * walked past the table. */
                ld_size zeroes = 1;
                while (zeroes < 8 && (i + zeroes) < max_syms) {
                    Elf32_Sym *t = &o->symtab[i + zeroes];
                    if (t->st_name || t->st_value || t->st_size) break;
                    zeroes++;
                }
                if (zeroes >= 8) break;
            }
            continue;
        }
        if (s->st_shndx == 0 /* SHN_UNDEF */) continue;
        if (s->st_size == 0) continue;
        ld_u32 sv = s->st_value + o->base;
        if (target < sv) continue;
        ld_u32 off = target - sv;
        if (off >= s->st_size) continue;
        if (off < best_off) { best = s; best_off = off; }
    }
    return best;
}

/* Userspace-visible Dl_info — must match the layout in
 * include/dlfcn.h.  Kept here as a local struct so ld.so doesn't
 * need to include the userspace header. */
typedef struct {
    const char *dli_fname;   /* DSO pathname / SONAME */
    void       *dli_fbase;   /* DSO load address */
    const char *dli_sname;   /* nearest symbol name */
    void       *dli_saddr;   /* nearest symbol address */
} dl_info_t;

LD_PUBLIC int __ldso_dladdr(const void *addr, void *info_out) {
    if (!info_out) return 0;
    dl_info_t *info = (dl_info_t *)info_out;
    info->dli_fname = 0;
    info->dli_fbase = 0;
    info->dli_sname = 0;
    info->dli_saddr = 0;

    ld_u32 target = (ld_u32)(unsigned long)addr;
    ld_obj_t *o = dl_obj_for_addr(target);
    if (!o) return 0;

    info->dli_fname = o->name;
    info->dli_fbase = (void *)(unsigned long)o->load_start;

    Elf32_Sym *s = dl_find_sym_in_obj(o, target);
    if (s) {
        info->dli_sname = o->strtab + s->st_name;
        info->dli_saddr = (void *)(unsigned long)(s->st_value + o->base);
    }
    return 1;   /* per POSIX: non-zero on success */
}

/* -------------------------------------------------------------------- *
 * Helper used by __ldso_dlsym for handle-specific lookups.  Public
 * for ld_dl.c's RTLD_NEXT walker too.
 * -------------------------------------------------------------------- */

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

/*
 * dl_iterate_phdr(3) — walk every loaded object (the executable plus all
 * shared libraries) and hand each one's program headers to the callback.
 * libgcc's DWARF unwinder, when built with USE_PT_GNU_EH_FRAME, calls this
 * to locate each module's PT_GNU_EH_FRAME (.eh_frame_hdr); it is what lets
 * a C++ exception unwind across shared-library boundaries.  The struct
 * layout matches the first four members of glibc's `struct dl_phdr_info`,
 * and we report the base size so the unwinder does not read the
 * dlpi_adds/dlpi_subs cache fields we don't maintain.
 */
struct ld_dl_phdr_info {
    ld_u32        dlpi_addr;
    const char   *dlpi_name;
    const void   *dlpi_phdr;
    ld_u16        dlpi_phnum;
};

LD_PUBLIC int __ldso_dl_iterate_phdr(
        int (*cb)(struct ld_dl_phdr_info *, ld_size, void *), void *data) {
    int ret = 0;
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        struct ld_dl_phdr_info info;
        info.dlpi_addr  = o->base;
        info.dlpi_name  = o->name;
        info.dlpi_phdr  = o->phdr;
        info.dlpi_phnum = (ld_u16)o->phnum;
        ret = cb(&info, sizeof(info), data);
        if (ret)
            break;
    }
    return ret;
}
