/*
 * ld_tls.c — install per-thread TLS (i386 variant 2 layout).
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

static ld_u32 align_up(ld_u32 v, ld_u32 a) {
    return a <= 1 ? v : (v + a - 1) & ~(a - 1);
}

/* Global GS base — recorded here so __tls_get_addr() can read it
 * cheaply without a syscall.  Set at the end of ld_setup_tls().
 * The same value is also stored at TCB[0] via the variant-II
 * self-pointer, but reading via gs:0 needs a tiny asm helper —
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
static ld_u32 ld_tls_cursor = 0;  /* |offset| of farthest module from TP */

int ld_setup_tls(void) {
    /* First pass: assign each PT_TLS module a negative offset from
     * the thread pointer, in load order.  The deepest dep gets the
     * highest |offset| (= farthest below the TP), the program gets
     * the lowest |offset| (= immediately below the TP) — same as
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
    if (cursor == 0) {
        /* No TLS-using objects.  Skip — gs:0 stays whatever the
         * kernel left it; libc that doesn't use __thread won't
         * touch it. */
        LD_DBG(ld_puts("ld.so: no PT_TLS modules — skipping TLS setup\n"));
        return 0;
    }
    if (cursor + LD_TLS_TCB_SIZE > LD_TLS_MAX_BYTES) {
        ld_puts("ld.so: TLS region exceeds cap\n");
        return -7; /* -E2BIG */
    }

    /* Allocate (TLS data) + TCB.  Round up to page so anonymous
     * mmap is happy and the TCB ends on a fixed alignment.  Stash
     * the cursor + total so __ldso_alloc_tls can replicate this
     * layout for new threads later. */
    ld_size total = align_up(cursor + LD_TLS_TCB_SIZE, 0x1000);
    ld_tls_cursor = cursor;
    ld_tls_total  = total;
    void *block = ld_mmap(0, total, LD_PROT_READ | LD_PROT_WRITE,
                          LD_MAP_PRIVATE | LD_MAP_ANON, -1, 0);
    if ((long)block < 0) {
        ld_puts("ld.so: TLS mmap failed\n");
        return -12; /* -ENOMEM */
    }

    /* Thread pointer = &block[cursor] (immediately above all TLS
     * data, and the TCB starts at that address). */
    ld_u32 tp = (ld_u32)(unsigned long)block + cursor;
    ld_u32 *tcb = (ld_u32 *)(unsigned long)tp;
    tcb[0] = tp;            /* self-pointer for `mov %gs:0,%eax` */
    tcb[1] = 0;             /* DTV slot reserved */

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
     * for any N — including negative offsets that hit our TLS
     * slots and zero that returns the TCB's self-pointer. */
    int rc = ld_sys_set_gsbase(tp);
    if (rc < 0) {
        ld_puts("ld.so: sys_set_gsbase failed: ");
        ld_putd((ld_u32)(-rc));
        ld_puts("\n");
        return rc;
    }
    ld_tp = tp;
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
 * Linear scan is fine — programs rarely have more than a handful
 * of TLS-using modules. */
/* Public surface — explicit visibility since the rest of ld.so is
 * built with -fvisibility=hidden. */
#define LD_PUBLIC __attribute__((visibility("default")))

/* Read the current thread's TP via the variant-II self-pointer at
 * gs:0.  This works for both the initial thread (where the kernel
 * set gs_base via sys_set_gsbase from ld_setup_tls) AND for
 * pthread-created threads (where the kernel set gs_base from
 * thr_param.tls_base via kern_thr_new).  Cheaper and more correct
 * than reading the static ld_tp — which only knows about the
 * initial thread's block. */
static inline ld_u32 current_tp(void) {
    ld_u32 tp;
    __asm__ volatile ("movl %%gs:0, %0" : "=r"(tp));
    return tp;
}

LD_PUBLIC void *__tls_get_addr(tls_index *idx) {
    if (!idx || idx->ti_module == 0) return 0;
    ld_u32 tp = current_tp();
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o->tls_modid == idx->ti_module)
            return (void *)(unsigned long)
                (tp - o->tls_offset + idx->ti_offset);
    }
    return 0;
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
    if ((long)block < 0) return 0;

    ld_u32 tp = (ld_u32)(unsigned long)block + ld_tls_cursor;
    ld_u32 *tcb = (ld_u32 *)(unsigned long)tp;
    tcb[0] = tp;            /* variant-II self-pointer */
    tcb[1] = 0;             /* DTV slot reserved */

    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o->tls_memsz == 0) continue;
        unsigned char *slot = (unsigned char *)(unsigned long)(tp - o->tls_offset);
        const unsigned char *src = (const unsigned char *)o->tls_image;
        ld_u32 i;
        for (i = 0; i < o->tls_filesz; i++) slot[i] = src[i];
        for (; i < o->tls_memsz; i++)        slot[i] = 0;
    }
    return (void *)(unsigned long)tp;
}

/* Optional: free a TLS block returned by __ldso_alloc_tls.  Called
 * from a thread's exit path after the thread is detached from its
 * own TP.  block is the address of the underlying allocation
 * (= tp - ld_tls_cursor). */
LD_PUBLIC void __ldso_free_tls(void *tp_ptr) {
    if (!tp_ptr || ld_tls_total == 0) return;
    /* munmap not yet wired through ld.so's io layer; for now we
     * leak the block.  libpthread doesn't currently support
     * detach-then-exit either, so the leak is bounded by
     * MAX_PTHREADS. */
    (void)tp_ptr;
}

/* GNU's libc historically also exports ___tls_get_addr (three
 * underscores) — same function, alternate name from the early
 * x86 PIC linker conventions.  Alias the entry. */
LD_PUBLIC void *___tls_get_addr(tls_index *idx)
    __attribute__((alias("__tls_get_addr")));
