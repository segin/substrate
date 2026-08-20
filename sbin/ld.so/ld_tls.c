/*
 * ld_tls.c - install per-thread TLS (i386 variant 2 layout).
 *
 * i386 variant-II TLS:
 *
 *   low addr                                              high addr
 *   +-------+-------+-------+-----+
 *   | TLS_n | ...   | TLS_1 | TCB |
 *   +-------+-------+-------+-----+
 *                                ^- thread pointer (gs:0)
 *
 *  TLS data lives at NEGATIVE offsets from the thread pointer.
 *  TCB is a single-word self-pointer at gs:0 so generated code
 *  like `mov %gs:0, %eax; mov -K(%eax), %ebx` resolves to the
 *  correct slot regardless of the actual GS base.
 *
 * Phase 4c scope:
 *   - Walk the loaded-object list, pick out PT_TLS modules.
 *   - Allocate a single contiguous block big enough for all
 *     PT_TLS images plus the TCB.
 *   - Lay each image out below the TCB; record per-module
 *     `tls_offset` so R_386_TLS_TPOFF relocations can be resolved.
 *   - Copy each image's file bytes into its slot; zero-fill the
 *     bss-style remainder (memsz - filesz).
 *   - Install the TCB pointer via sys_set_gsbase.
 *
 * Limits we accept for the first cut:
 *   - Single-thread.  pthread_create-style threads will need a
 *     per-thread allocation in libpthread; the program-startup
 *     path here only sets up the initial thread.
 *   - No DTV.  Local-exec and initial-exec models work because
 *     R_386_TLS_TPOFF resolves to a constant offset; full GD/LD
 *     models are deferred (require __tls_get_addr).
 */

#include "ld.h"

#define LD_TLS_TCB_SIZE 8       /* TCB[0]=self-ptr, TCB[1]=DTV reserved */
#define LD_TLS_MAX_BYTES 0x8000 /* sanity cap on total per-thread block */

/*
 * Surplus static TLS.
 *
 * The startup layout below is one-shot: it walks the objects loaded by the
 * initial BFS and hands each a fixed negative offset from the thread pointer.
 * A module that arrives later via dlopen missed that pass, so it used to keep
 * tls_modid 0 / tls_offset 0 and every TLS relocation against it had to fail
 * (see ld_reloc.c) rather than emit positive %gs offsets over the TCB.
 *
 * That made any dlopen of a PT_TLS-carrying object impossible -- which is
 * every C++ shared module, since linking libstdc++ pulls TLS in.  It is
 * exactly what a plugin host like TDE's tdeinit/KLibLoader does for a living.
 *
 * So reserve a slab below the startup modules and carve dlopen'd modules out
 * of it (ld_tls_add_module).  This is what glibc calls surplus static TLS, and
 * it serves the initial-exec model -- R_386_TLS_TPOFF against a fixed offset,
 * which is what those modules actually emit.  It is deliberately NOT a
 * growable DTV: a module needing more than the surplus is refused with a
 * diagnostic instead of silently corrupting a neighbouring module's slot.
 */
#define LD_TLS_SURPLUS      0x1000  /* bytes reserved for dlopen'd modules */
#define LD_TLS_SURPLUS_MODS 16      /* extra DTV slots for the same */

static ld_u32 align_up(ld_u32 v, ld_u32 a) {
    return a <= 1 ? v : (v + a - 1) & ~(a - 1);
}

/* Global GS base - recorded here so __tls_get_addr() can read it
 * cheaply without a syscall.  Set at the end of ld_setup_tls().
 * The same value is also stored at TCB[0] via the variant-II
 * self-pointer, but reading via gs:0 needs a tiny asm helper -
 * doing it from C through this variable is portable enough.
 *
 * NOTE: this is the INITIAL thread's TP.  Each pthread_create-
 * spawned thread has its own per-thread block (allocated by
 * __ldso_alloc_tls below); __tls_get_addr() reads gs:0 instead of
 * ld_tp when running on a non-initial thread.  Since gs:0 *is* the
 * TP (TCB self-pointer), this works for both. */
static ld_u32 ld_tp = 0;

/* Total per-thread block size (cursor + TCB), cached after the
 * first ld_setup_tls() pass so __ldso_alloc_tls can allocate
 * additional blocks for pthread_create-spawned threads with the
 * same layout. */
static ld_u32 ld_tls_total = 0;
static ld_u32 ld_tls_cursor = 0;  /* block base to TP: startup modules + surplus */
static ld_u32 ld_tls_modcount = 0; /* number of PT_TLS modules (DTV length) */

/* Highest |offset| actually handed out so far.  Startup leaves this at the end
 * of the startup modules; ld_tls_add_module() grows it into the surplus, up to
 * ld_tls_cursor.  The gap between the two is the free surplus. */
static ld_u32 ld_tls_alloc_cursor = 0;
/* Largest module id the DTV has room for (modcount + LD_TLS_SURPLUS_MODS). */
static ld_u32 ld_tls_modcap = 0;

/*
 * Registry of live per-thread TLS blocks, threaded through the blocks
 * themselves: one word sits just past the DTV in every block and holds the
 * next thread's TP (0 terminates).  ld_tls_thread_head is the list head.
 *
 * A registry is unavoidable for the initial-exec model.  An IE reference
 * compiles to a fixed %gs offset with no function call, so there is no hook at
 * which a thread could fault in a module that appeared after it started --
 * the data simply has to be there.  When dlopen adds a module, its image is
 * therefore copied into EVERY live thread's block right away.
 *
 * The general-dynamic model does have a hook (__tls_get_addr), so that path is
 * lazy instead: see __ldso_tls_update() below.
 *
 * Mutated under the dlopen lock, which pthread_create's TLS allocation and
 * dlopen both take, so the list never changes underfoot.
 */
static ld_u32 ld_tls_thread_head = 0;

/* Byte offset from TP to the registry link word (immediately past the DTV). */
static ld_u32 ld_tls_link_off(void) {
    return LD_TLS_TCB_SIZE + (ld_tls_modcap + 1) * 4;
}

static ld_u32 *ld_tls_link_at(ld_u32 tp) {
    return (ld_u32 *)(unsigned long)(tp + ld_tls_link_off());
}

static void ld_tls_register(ld_u32 tp) {
    *ld_tls_link_at(tp) = ld_tls_thread_head;
    ld_tls_thread_head = tp;
}

static void ld_tls_unregister(ld_u32 tp) {
    ld_u32 *pp = &ld_tls_thread_head;
    while (*pp) {
        if (*pp == tp) { *pp = *ld_tls_link_at(tp); return; }
        pp = ld_tls_link_at(*pp);
    }
}

/*
 * Copy a module's PT_TLS image into one thread's slot and publish it in that
 * thread's DTV.  Idempotent: a non-zero DTV entry means it is already there.
 */
static void ld_tls_init_in(ld_u32 tp, ld_obj_t *o) {
    ld_u32 *dtv = (ld_u32 *)(unsigned long)(tp + LD_TLS_TCB_SIZE);

    if (o->tls_modid == 0 || o->tls_modid > ld_tls_modcap) return;
    if (dtv[o->tls_modid]) return;                  /* already initialized */

    unsigned char *slot = (unsigned char *)(unsigned long)(tp - o->tls_offset);
    const unsigned char *src = (const unsigned char *)o->tls_image;
    ld_u32 i;
    for (i = 0; i < o->tls_filesz; i++) slot[i] = src[i];
    for (; i < o->tls_memsz; i++)       slot[i] = 0;

    dtv[o->tls_modid] = tp - o->tls_offset;
}

/* Build the Dynamic Thread Vector for a freshly-laid-out per-thread block and
 * publish it at TCB[1] (gs:4).  DTV[0] is the module count; DTV[modid] is the
 * base of that module's TLS block in this thread (TP - module->tls_offset).
 * __tls_get_addr in libc reads gs:4 and returns DTV[ti_module] + ti_offset.
 * The DTV is carved out of the block immediately above the TCB (the block was
 * sized to leave room). */
static void ld_fill_dtv(ld_u32 tp) {
    ld_u32 *dtv = (ld_u32 *)(unsigned long)(tp + LD_TLS_TCB_SIZE);
    dtv[0] = ld_tls_modcount;
    /* Zero every slot first.  An empty slot is what marks a module as "not yet
     * present in this thread", which is the signal libc's __tls_get_addr uses
     * to call back into __ldso_tls_update().  mmap hands back zeroed pages, so
     * this only matters for a block being re-filled. */
    for (ld_u32 m = 1; m <= ld_tls_modcap; m++) dtv[m] = 0;
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o->tls_memsz == 0 || o->tls_modid == 0) continue;
        if (o->tls_modid > ld_tls_modcap) continue;
        dtv[o->tls_modid] = tp - o->tls_offset;   /* module's TLS block base */
    }
    ((ld_u32 *)(unsigned long)tp)[1] = (ld_u32)(unsigned long)dtv;  /* TCB[1] */
}

int ld_setup_tls(void) {
    /* First pass: assign each PT_TLS module a negative offset from
     * the thread pointer, in load order.  The deepest dep gets the
     * highest |offset| (= farthest below the TP), the program gets
     * the lowest |offset| (= immediately below the TP) - same as
     * what static linkers compute when relocating the program.
     *
     * Module IDs are 1-based (0 means "no TLS"), allocated in the
     * same load order so GD/LD relocations can find the matching
     * object via a linear scan in __tls_get_addr(). */
    ld_u32 cursor = 0;     /* running |offset| from TP, grows with each module */
    ld_u32 max_align = LD_TLS_TCB_SIZE;
    ld_u32 next_modid = 1;
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o->tls_memsz == 0) continue;
        ld_u32 align = o->tls_align ? o->tls_align : 1;
        cursor = align_up(cursor + o->tls_memsz, align);
        o->tls_offset = cursor;     /* slot starts at TP - cursor */
        o->tls_modid  = next_modid++;
        if (align > max_align) max_align = align;
    }
    ld_tls_modcount = next_modid - 1;
    /* Round the final cursor up to the LARGEST module alignment.  Each
     * module's slot lives at tp - tls_offset with tls_offset a multiple
     * of that module's own alignment - so the slot is aligned iff tp
     * itself is aligned to it.  tp = block + cursor with block page-
     * aligned, so aligning the cursor to max_align aligns tp for every
     * module.  (Assumes max_align <= PAGE_SIZE, true for any real DSO.)
     * Without this, a 16-byte-aligned TLS block laid out before a
     * smaller-aligned one could land at tp-offset ≡ 4 (mod 16). */
    cursor = align_up(cursor, max_align);

    /* Everything handed out so far is startup; the surplus starts here. */
    ld_tls_alloc_cursor = cursor;
    ld_tls_modcap       = ld_tls_modcount + LD_TLS_SURPLUS_MODS;

    /* Reserve the surplus BELOW the startup modules (i.e. at a larger
     * |offset|), so the offsets just assigned stay valid: TP moves up by the
     * surplus and each startup module keeps sitting at TP - its own offset.
     * Keep it a multiple of max_align so TP stays aligned for every module.
     *
     * Note this runs even when cursor == 0 -- a program whose startup objects
     * carry no PT_TLS at all (a plain C executable: substrate's libc keeps no
     * __thread state) still needs a thread pointer and a slab if it is later
     * going to dlopen a C++ module.  That case used to return early with no
     * TLS block, which is why dlopen'ing even a trivial C++ plugin from a C
     * host failed. */
    cursor += align_up(LD_TLS_SURPLUS, max_align);

    if (cursor + LD_TLS_TCB_SIZE > LD_TLS_MAX_BYTES) {
        ld_puts("ld.so: TLS region exceeds cap\n");
        return -7; /* -E2BIG */
    }

    /* Allocate (TLS data) + TCB.  Round up to page so anonymous
     * mmap is happy and the TCB ends on a fixed alignment.  Stash
     * the cursor + total so __ldso_alloc_tls can replicate this
     * layout for new threads later. */
    /* + 4 for the registry link word that follows the DTV. */
    ld_size total = align_up(cursor + LD_TLS_TCB_SIZE
                             + (ld_tls_modcap + 1) * 4 + 4, 0x1000);
    ld_tls_cursor = cursor;
    ld_tls_total  = total;
    void *block = ld_mmap(0, total, LD_PROT_READ | LD_PROT_WRITE,
                          LD_MAP_PRIVATE | LD_MAP_ANON, -1, 0);
    if (ld_mmap_failed(block)) {
        ld_puts("ld.so: TLS mmap failed\n");
        return -12; /* -ENOMEM */
    }

    /* Thread pointer = &block[cursor] (immediately above all TLS
     * data, and the TCB starts at that address). */
    ld_u32 tp = (ld_u32)(unsigned long)block + cursor;
    ld_u32 *tcb = (ld_u32 *)(unsigned long)tp;
    tcb[0] = tp;            /* self-pointer for `mov %gs:0,%eax` */
    ld_fill_dtv(tp);        /* TCB[1] = DTV for the GD/LD model */

    /* Second pass: copy each module's PT_TLS image into its slot
     * and zero-pad the BSS portion.  Slot starts at tp - tls_offset. */
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o->tls_memsz == 0) continue;
        unsigned char *slot = (unsigned char *)(unsigned long)(tp - o->tls_offset);
        const unsigned char *src = (const unsigned char *)o->tls_image;
        ld_u32 i;
        for (i = 0; i < o->tls_filesz; i++) slot[i] = src[i];
        for (; i < o->tls_memsz; i++)        slot[i] = 0;
        if (ld_debug) {
            ld_puts("ld.so: tls "); ld_puts(o->name);
            ld_puts(" memsz="); ld_putx(o->tls_memsz);
            ld_puts(" filesz="); ld_putx(o->tls_filesz);
            ld_puts(" offset=-"); ld_putx(o->tls_offset);
            ld_puts("\n");
        }
    }

    /* Install the GS base.  After this, %gs:N maps to (tp + N)
     * for any N - including negative offsets that hit our TLS
     * slots and zero that returns the TCB's self-pointer. */
    int rc = ld_sys_set_gsbase(tp);
    if (rc < 0) {
        ld_puts("ld.so: sys_set_gsbase failed: ");
        ld_putd((ld_u32)(-rc));
        ld_puts("\n");
        return rc;
    }
    ld_tp = tp;
    ld_tls_register(tp);        /* initial thread joins the registry */
    if (ld_debug) {
        ld_puts("ld.so: TLS installed, tp="); ld_putx(tp); ld_puts("\n");
    }
    return 0;
}

/* C++ thread_local (and __thread in DSOs) compiles to GD/LD model
 * calls into __tls_get_addr.  GCC emits:
 *
 *     leal  X@TLSGD(,%ebx,1), %eax
 *     call  ___tls_get_addr@PLT
 *
 * with X@TLSGD resolving (via two relocations on a GOT pair) to a
 * tls_index{module, offset} struct.  The helper returns the runtime
 * address of variable X within the current thread.
 *
 * Substrate's loader stores per-module TLS offsets in ld_obj_t.
 * Linear scan is fine - programs rarely have more than a handful
 * of TLS-using modules. */
/* Public surface - explicit visibility since the rest of ld.so is
 * built with -fvisibility=hidden. */
#define LD_PUBLIC __attribute__((visibility("default")))

/* Read the current thread's TP via the variant-II self-pointer at
 * gs:0.  This works for both the initial thread (where the kernel
 * set gs_base via sys_set_gsbase from ld_setup_tls) AND for
 * pthread-created threads (where the kernel set gs_base from
 * thr_param.tls_base via kern_thr_new).  Cheaper and more correct
 * than reading the static ld_tp - which only knows about the
 * initial thread's block. */
static inline ld_u32 current_tp(void) {
    ld_u32 tp;
    __asm__ volatile ("movl %%gs:0, %0" : "=r"(tp));
    return tp;
}

/*
 * Give a module loaded after startup (dlopen) a TLS slot out of the surplus.
 *
 * Called from the dlopen path once the new objects and their DT_NEEDED
 * closure are loaded, but BEFORE they are relocated -- the TLS relocations
 * read tls_modid/tls_offset, and a module still holding 0 is what the
 * "loaded after startup" failures in ld_reloc.c report.
 *
 * EVERY live thread is initialized, not just the caller.  The initial-exec
 * model compiles to a bare %gs offset with no function call, so a thread that
 * predates the dlopen has no hook at which it could fault the module in -- the
 * data has to already be there when it first reads the address.  The registry
 * exists for exactly this.  (General-dynamic references go through
 * __tls_get_addr and are additionally handled lazily, which covers a thread
 * created concurrently with this call; see __ldso_tls_update.)
 *
 * Returns 0 on success, -1 if the surplus is exhausted (diagnosed).
 */
int ld_tls_add_module(ld_obj_t *o) {
    if (!o || o->tls_memsz == 0) return 0;   /* nothing to lay out */
    if (o->tls_modid != 0)       return 0;   /* already placed at startup */

    if (ld_tls_total == 0) {
        /* ld_setup_tls() never ran or bailed; there is no block to carve. */
        ld_puts("ld.so: dlopen of TLS module with no TLS block: ");
        ld_puts(o->name); ld_puts("\n");
        return -1;
    }

    ld_u32 align  = o->tls_align ? o->tls_align : 1;
    ld_u32 newcur = align_up(ld_tls_alloc_cursor + o->tls_memsz, align);

    if (newcur > ld_tls_cursor || ld_tls_modcount + 1 > ld_tls_modcap) {
        ld_puts("ld.so: surplus static TLS exhausted loading ");
        ld_puts(o->name);
        ld_puts(" (need "); ld_putx(o->tls_memsz);
        ld_puts(", have "); ld_putx(ld_tls_cursor - ld_tls_alloc_cursor);
        ld_puts(")\n");
        return -1;
    }

    o->tls_offset = newcur;
    o->tls_modid  = ++ld_tls_modcount;
    ld_tls_alloc_cursor = newcur;

    /* Initialize the new slot in every live thread, so an initial-exec
     * reference from a thread that predates this dlopen reads the module's
     * initialization image rather than whatever the surplus happened to hold. */
    unsigned live = 0;
    for (ld_u32 tp = ld_tls_thread_head; tp; tp = *ld_tls_link_at(tp)) {
        ld_u32 *dtv = (ld_u32 *)(unsigned long)(tp + LD_TLS_TCB_SIZE);
        ld_tls_init_in(tp, o);
        dtv[0] = ld_tls_modcount;
        live++;
    }

    if (ld_debug) {
        ld_puts("ld.so: tls(dlopen) initialized in ");
        ld_putd(live); ld_puts(" live thread(s)\n");
    }
    if (ld_debug) {
        ld_puts("ld.so: tls(dlopen) "); ld_puts(o->name);
        ld_puts(" memsz="); ld_putx(o->tls_memsz);
        ld_puts(" offset=-"); ld_putx(o->tls_offset);
        ld_puts(" modid="); ld_putd(o->tls_modid);
        ld_puts("\n");
    }
    return 0;
}

/*
 * Lazy per-thread DTV fill-in — the general-dynamic half of dlopen'd TLS.
 *
 * libc, not ld.so, owns __tls_get_addr: a GD reference has to resolve at LINK
 * time, and every dynamic binary DT_NEEDEDs libc while ld.so is the
 * interpreter rather than a link-time library.  libc's version is a two-load
 * fast path, DTV[module] + offset, and it calls in here only when that slot is
 * still empty -- which is precisely the case of a module this thread has never
 * seen, i.e. one dlopen'd by somebody else.
 *
 * So the DTV grows per thread, on demand, at the first access.  Returns the
 * base of the module's block in the calling thread, or 0 for a module id that
 * does not exist (libc then adds ti_offset to 0 and faults, which is the same
 * outcome as any other bad TLS reference).
 *
 * Takes the dlopen lock: ld_tls_add_module() may be walking the registry, and
 * this reads the same object list.  Recursive, so a constructor calling into
 * TLS while dlopen holds it is fine.
 */
LD_PUBLIC void *__ldso_tls_update(unsigned long modid) {
    void *base = 0;

    if (modid == 0 || modid > ld_tls_modcap) return 0;

    ld_dl_lock();
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o->tls_modid != (ld_u32)modid) continue;
        ld_u32 tp = current_tp();
        ld_tls_init_in(tp, o);              /* copies image, sets DTV slot */
        base = (void *)(unsigned long)(tp - o->tls_offset);
        break;
    }
    ld_dl_unlock();
    return base;
}

static void *ld_tls_get_addr(tls_index *idx) {
    if (!idx || idx->ti_module == 0) return 0;
    ld_u32 tp = current_tp();
    ld_u32 *dtv = (ld_u32 *)(unsigned long)(tp + LD_TLS_TCB_SIZE);

    /* Same contract as libc's copy: trust the DTV, fill it in on a miss. */
    if (idx->ti_module <= ld_tls_modcap && dtv[idx->ti_module])
        return (void *)(unsigned long)(dtv[idx->ti_module] + idx->ti_offset);

    void *base = __ldso_tls_update(idx->ti_module);
    if (!base) return 0;
    return (void *)((unsigned long)base + idx->ti_offset);
}

/* The normal (stack-argument) entry point.  The primary provider is libc.so.0
 * (which defines __tls_get_addr/___tls_get_addr self-contained via the DTV at
 * gs:4, so they resolve at link time against libc); ld.so keeps its own copy
 * as a fallback for binaries that don't pull in libc. */
LD_PUBLIC void *__tls_get_addr(tls_index *idx) {
    return ld_tls_get_addr(idx);
}

/* Allocate a per-thread TLS block for pthread_create.  Mirrors the
 * second-pass logic in ld_setup_tls(): mmap a block big enough for
 * every PT_TLS module plus the TCB, copy each module's initialization
 * image into its slot, set TCB[0] to the TP self-pointer.  Returns
 * the TP (= address of the TCB), which libpthread passes to the
 * kernel as thr_param.tls_base; the kernel installs it as the new
 * thread's gs_base.
 *
 * Returns NULL on failure (no TLS-using modules, mmap failure,
 * called before initial-thread setup ran). */
LD_PUBLIC void *__ldso_alloc_tls(void) {
    if (ld_tls_total == 0 || ld_tls_cursor == 0) {
        /* No PT_TLS at all, or ld_setup_tls() hasn't run yet. */
        return 0;
    }
    void *block = ld_mmap(0, ld_tls_total, LD_PROT_READ | LD_PROT_WRITE,
                          LD_MAP_PRIVATE | LD_MAP_ANON, -1, 0);
    if (ld_mmap_failed(block)) return 0;

    ld_u32 tp = (ld_u32)(unsigned long)block + ld_tls_cursor;
    ld_u32 *tcb = (ld_u32 *)(unsigned long)tp;
    tcb[0] = tp;            /* variant-II self-pointer */

    /* Under the dlopen lock: a concurrent dlopen walks the registry to
     * initialize a new module in every live thread, and this both fills the
     * DTV from the same object list and joins that registry. */
    ld_dl_lock();
    ld_fill_dtv(tp);        /* per-thread DTV at TCB[1] */
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o->tls_memsz == 0) continue;
        unsigned char *slot = (unsigned char *)(unsigned long)(tp - o->tls_offset);
        const unsigned char *src = (const unsigned char *)o->tls_image;
        ld_u32 i;
        for (i = 0; i < o->tls_filesz; i++) slot[i] = src[i];
        for (; i < o->tls_memsz; i++)        slot[i] = 0;
    }
    ld_tls_register(tp);
    ld_dl_unlock();
    return (void *)(unsigned long)tp;
}

/* Optional: free a TLS block returned by __ldso_alloc_tls.  Called
 * from a thread's exit path after the thread is detached from its
 * own TP.  block is the address of the underlying allocation
 * (= tp - ld_tls_cursor). */
LD_PUBLIC void __ldso_free_tls(void *tp_ptr) {
    if (!tp_ptr || ld_tls_total == 0) return;
    /* Leave the registry before the memory goes back, or a later dlopen would
     * walk into an unmapped block. */
    ld_dl_lock();
    ld_tls_unregister((ld_u32)(unsigned long)tp_ptr);
    ld_dl_unlock();
    /* The block was mmap'd at (tp - ld_tls_cursor) for ld_tls_total
     * bytes (see __ldso_alloc_tls / ld_setup_tls); hand the whole
     * allocation back.  Without this every thread exit leaked >= 1
     * page. */
    void *block = (void *)((unsigned long)tp_ptr - ld_tls_cursor);
    ld_munmap(block, ld_tls_total);
}

/* The i386 GD/LD sequence GCC emits is:
 *     leal  x@TLSGD(,%ebx,1), %eax
 *     call  ___tls_get_addr@PLT
 * i.e. the tls_index pointer is passed in %eax, NOT on the stack.  So
 * ___tls_get_addr (three underscores) takes its argument with regparm(1);
 * it is NOT a plain alias of the stack-convention __tls_get_addr - aliasing
 * the two would make ___tls_get_addr read a garbage "pointer" off the stack. */
LD_PUBLIC __attribute__((regparm(1))) void *___tls_get_addr(tls_index *idx) {
    return ld_tls_get_addr(idx);
}
