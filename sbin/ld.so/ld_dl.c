/*
 * ld_dl.c - runtime dlopen / dlsym / dlclose / dlerror / dladdr
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
 * dl-API serialization lock (LDSO-06)
 *
 * The startup path (ld_main) is single-threaded, but once the program
 * is running any thread may call dlopen/dlsym/dlclose concurrently -
 * and with SMP those threads run in parallel.  dlopen mutates the
 * global object list (a non-atomic multi-store append) and flips
 * per-object relocation guards; dlsym/dlerror read shared state.  A
 * single process-wide futex mutex serializes them.
 *
 * The lock is RECURSIVE: dlopen holds it while running the new
 * objects' constructors (ld_run_init_arrays), and a constructor may
 * legitimately call dlopen/dlsym itself (optional-symbol probing is
 * common) - a plain mutex would self-deadlock.  Owner is the kernel
 * thread id (thr_self), always valid unlike gs:0 for a no-TLS program.
 * The futex word is three-state (0=free, 1=held, 2=held+waiters) so
 * the uncontended acquire is a lone compare-exchange with no syscall.
 * -------------------------------------------------------------------- */

static int g_dl_futex = 0;      /* 0=free, 1=held, 2=held+waiters */
static int g_dl_owner = 0;      /* thr_self() of the holder, 0 if free */
static int g_dl_depth = 0;      /* recursion count */

static void dl_lock(void) {
    int me = ld_thr_self();
    if (g_dl_owner == me) { g_dl_depth++; return; }   /* re-entrant */
    int c = __sync_val_compare_and_swap(&g_dl_futex, 0, 1);
    if (c != 0) {
        if (c != 2) c = __sync_lock_test_and_set(&g_dl_futex, 2);
        while (c != 0) {
            ld_futex(&g_dl_futex, LD_FUTEX_WAIT, 2);
            c = __sync_lock_test_and_set(&g_dl_futex, 2);
        }
    }
    g_dl_owner = me;
    g_dl_depth = 1;
}

static void dl_unlock(void) {
    if (--g_dl_depth > 0) return;                       /* still held by us */
    g_dl_owner = 0;
    /* If there were waiters (value was 2), wake one after releasing. */
    if (__sync_fetch_and_sub(&g_dl_futex, 1) != 1) {
        g_dl_futex = 0;
        ld_futex(&g_dl_futex, LD_FUTEX_WAKE, 1);
    }
}

/* True iff `h` is a live object handle (a node currently on the loaded-
 * object list).  dlopen returns ld_obj_t pointers as opaque handles;
 * a caller passing a stale or garbage handle must not be dereferenced.
 * The list is short (<= LD_MAX_OBJS), so a linear walk is cheap. */
static int dl_handle_valid(const void *h) {
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next)
        if ((const void *)o == h) return 1;
    return 0;
}

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

static void *dlopen_locked(const char *path, int flags) {
    /* RTLD_LAZY/NOW/GLOBAL/LOCAL all behave eager+global today; only
     * RTLD_NOLOAD changes what we do. */
    if (!path) {
        /* dlopen(NULL) - return a handle representing the main
         * program (== head of loaded-object list). */
        return ld_obj_list();
    }
    if (flags & RTLD_NOLOAD) {
        /* Probe only: map nothing.  Hand back a handle iff the object is
         * already loaded -- and count the reference, because the caller
         * still owes a matching dlclose (POSIX treats a successful
         * RTLD_NOLOAD open like any other).  Callers use this to detect an
         * optional dependency that some other object already pulled in
         * without dragging it in themselves. */
        ld_obj_t *have = ld_obj_find_loaded(path);
        if (!have) {
            ld_dl_error("dlopen(\"", path,
                        "\"): not already loaded (RTLD_NOLOAD)", 0);
            return 0;
        }
        have->refcount++;
        have->finalized = 0;
        return have;
    }
    /* Snapshot the list end so any failure below unwinds every object
     * this call appended - leaving unrelocated, half-loaded objects in
     * the global scope (the old behaviour) let later lookups bind to
     * garbage. */
    ld_obj_t *snap_tail;
    ld_size   snap_count;
    ld_obj_savepoint(&snap_tail, &snap_count);

    ld_obj_t *o = ld_load_object(path);
    if (!o) {
        ld_dl_error("dlopen(\"", path, "\"): load failed", 0);
        ld_obj_restore(snap_tail, snap_count);
        return 0;
    }
    /* Walk newly-loaded objects' DT_NEEDED so transitive deps
     * are pulled in.  A missing dependency is fatal (matches the
     * startup BFS), not silently ignored. */
    for (ld_obj_t *cur = o; cur; cur = cur->next) {
        if (!cur->dynamic || !cur->strtab) continue;
        for (Elf32_Dyn *d = cur->dynamic; d->d_tag != DT_NULL; d++) {
            if (d->d_tag != DT_NEEDED) continue;
            const char *soname = cur->strtab + d->d_un.d_val;
            if (!ld_load_object(soname)) {
                ld_dl_error("dlopen(\"", path, "\"): missing dependency ",
                            soname);
                ld_obj_restore(snap_tail, snap_count);
                return 0;
            }
        }
    }
    for (ld_obj_t *r = ld_obj_list(); r; r = r->next) {
        if (ld_relocate(r) != 0) {
            ld_dl_error("dlopen(\"", path, "\"): relocation failed", 0);
            ld_obj_restore(snap_tail, snap_count);
            return 0;
        }
    }
    /* Apply W^X + RELRO to the newly-mapped objects (idempotent guard
     * skips ones already protected) before their constructors run. */
    for (ld_obj_t *r = ld_obj_list(); r; r = r->next)
        ld_protect_object(r);
    ld_run_init_arrays();
    /* Count this reference.  A repeat dlopen of the same object (dedup)
     * returns the same handle and bumps the count again, so it takes an
     * equal number of dlclose calls to run its destructors.  If a prior
     * dlclose already finalized it, clear the flag so a future last
     * close can fire destructors again (constructors are NOT re-run: we
     * never unmapped, so the object's state persists). */
    o->refcount++;
    o->finalized = 0;
    return o;
}

LD_PUBLIC void *__ldso_dlopen(const char *path, int flags) {
    dl_lock();
    void *r = dlopen_locked(path, flags);
    dl_unlock();
    return r;
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

static void *dlsym_locked(void *handle, const char *name, void *caller_pc) {
    if (!name) { ld_dl_error("dlsym: null name", 0, 0, 0); return 0; }

    /* RTLD_DEFAULT, or the dlopen(NULL) handle (the main program at the
     * list head): both mean "search the whole global scope" per POSIX,
     * not just the executable's own symbol table. */
    if (handle == LD_RTLD_DEFAULT || handle == (void *)ld_obj_list()) {
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

    /* Object handle - search just this object's symbol table.  Reject a
     * handle that isn't a live loaded object rather than dereferencing
     * a stale/garbage pointer. */
    if (!dl_handle_valid(handle)) {
        ld_dl_error("dlsym: invalid handle", 0, 0, 0);
        return 0;
    }
    ld_obj_t *target = (ld_obj_t *)handle;
    ld_u32 v = ld_lookup_in_obj(target, name);
    if (!v) ld_dl_error("dlsym(\"", name, "\"): not found in handle", 0);
    return (void *)(unsigned long)v;
}

LD_PUBLIC void *__ldso_dlsym(void *handle, const char *name, void *caller_pc) {
    dl_lock();
    void *r = dlsym_locked(handle, name, caller_pc);
    dl_unlock();
    return r;
}

/* -------------------------------------------------------------------- *
 * dlclose - run fini, decrement (notional) refcount, do not unmap.
 *
 * Real unmap requires reverse-dependency tracking (every symbol that
 * was resolved through this object's hash must still be reachable
 * elsewhere or never called) - Phase 5+ concern.  For now dlclose
 * delivers its most useful behaviour: runs DT_FINI / DT_FINI_ARRAY
 * for the handle so global destructors fire.
 * -------------------------------------------------------------------- */

static int dl_streq(const char *a, const char *b);   /* defined below */

/* True iff some other loaded object lists `o`'s SONAME in its
 * DT_NEEDED - i.e. o is still a live dependency and must not be
 * finalized even though its own dlopen refcount hit zero. */
static int dl_object_still_needed(const ld_obj_t *o) {
    for (ld_obj_t *c = ld_obj_list(); c; c = c->next) {
        if (c == o || !c->dynamic || !c->strtab) continue;
        for (Elf32_Dyn *d = c->dynamic; d->d_tag != DT_NULL; d++) {
            if (d->d_tag != DT_NEEDED) continue;
            if (dl_streq(c->strtab + d->d_un.d_val, o->name)) return 1;
        }
    }
    return 0;
}

static int dlclose_locked(void *handle) {
    if (!handle || handle == LD_RTLD_DEFAULT || handle == LD_RTLD_NEXT ||
        !dl_handle_valid(handle)) {
        ld_dl_error("dlclose: invalid handle", 0, 0, 0);
        return -1;
    }
    ld_obj_t *o = (ld_obj_t *)handle;
    if (o->refcount > 0) o->refcount--;
    if (o->refcount > 0) return 0;              /* other dlopens still hold it */
    if (dl_object_still_needed(o)) return 0;    /* a live DSO DT_NEEDEDs it */
    if (o->finalized) return 0;
    /* Run DT_FINI_ARRAY in REVERSE registration order, then DT_FINI
     * - matches the order glibc uses. */
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

LD_PUBLIC int __ldso_dlclose(void *handle) {
    dl_lock();
    int r = dlclose_locked(handle);
    dl_unlock();
    return r;
}

/* -------------------------------------------------------------------- *
 * dladdr - given an address, find the DSO + symbol containing it.
 * -------------------------------------------------------------------- */

/* Walk a DSO's dynamic symbol table for the symbol whose
 * [st_value, st_value + st_size) range contains `target`.  Skip
 * undefined and zero-size symbols.  Returns 0 if no match. */
/* Number of entries in a DSO's .dynsym.  Derived exactly from the
 * hash section rather than guessed: DT_HASH stores nchains (== symbol
 * count) at word[1]; DT_GNU_HASH requires finding the max symbol index
 * reachable through its buckets/chains and adding 1.  Returns 0 if
 * neither hash is present (dladdr then can't safely walk). */
static ld_u32 dl_dynsym_count(const ld_obj_t *o) {
    if (o->hash)
        return o->hash[1];              /* nchains == nsyms (SysV) */
    if (o->gnu_hash) {
        const ld_u32 *h = o->gnu_hash;
        ld_u32 nbuckets   = h[0];
        ld_u32 symbias    = h[1];
        ld_u32 bloom_size = h[2];
        const ld_u32 *buckets = h + 4 + bloom_size;
        const ld_u32 *chain   = buckets + nbuckets;
        ld_u32 maxidx = symbias;        /* symbols [0, symbias) aren't hashed */
        for (ld_u32 b = 0; b < nbuckets; b++) {
            ld_u32 idx = buckets[b];
            if (idx < symbias) continue;
            /* Walk to this bucket's end-of-chain (low bit set). */
            for (;;) {
                if (idx >= maxidx) maxidx = idx;
                if (chain[idx - symbias] & 1) break;
                idx++;
            }
        }
        return maxidx + 1;
    }
    return 0;
}

static Elf32_Sym *dl_find_sym_in_obj(const ld_obj_t *o, ld_u32 target) {
    if (!o->symtab) return 0;
    /* Bound the walk by the exact dynsym count from the hash section.
     * The old 65536 + zero-run heuristic reinterpreted strtab bytes as
     * symbols and marched ~1 MB past a small DSO's mapping → SIGSEGV. */
    ld_u32 nsyms = dl_dynsym_count(o);
    if (nsyms == 0) return 0;
    if (nsyms > 65536) nsyms = 65536;   /* backstop vs a corrupt count */
    Elf32_Sym *best = 0;
    ld_u32 best_off = 0xFFFFFFFFu;
    for (ld_u32 i = 0; i < nsyms; i++) {
        Elf32_Sym *s = &o->symtab[i];
        if (o->strsz && s->st_name >= o->strsz) continue;  /* bad name idx */
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

/* Userspace-visible Dl_info - must match the layout in
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
 * dl_iterate_phdr(3) - walk every loaded object (the executable plus all
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
