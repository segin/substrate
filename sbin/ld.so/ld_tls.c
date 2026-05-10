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

int ld_setup_tls(void) {
    /* First pass: assign each PT_TLS module a negative offset from
     * the thread pointer, in load order.  The deepest dep gets the
     * highest |offset| (= farthest below the TP), the program gets
     * the lowest |offset| (= immediately below the TP) — same as
     * what static linkers compute when relocating the program. */
    ld_u32 cursor = 0;     /* running |offset| from TP, grows with each module */
    ld_u32 max_align = LD_TLS_TCB_SIZE;
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (o->tls_memsz == 0) continue;
        ld_u32 align = o->tls_align ? o->tls_align : 1;
        cursor = align_up(cursor + o->tls_memsz, align);
        o->tls_offset = cursor;     /* slot starts at TP - cursor */
        if (align > max_align) max_align = align;
    }
    if (cursor == 0) {
        /* No TLS-using objects.  Skip — gs:0 stays whatever the
         * kernel left it; libc that doesn't use __thread won't
         * touch it. */
        ld_puts("ld.so: no PT_TLS modules — skipping TLS setup\n");
        return 0;
    }
    if (cursor + LD_TLS_TCB_SIZE > LD_TLS_MAX_BYTES) {
        ld_puts("ld.so: TLS region exceeds cap\n");
        return -7; /* -E2BIG */
    }

    /* Allocate (TLS data) + TCB.  Round up to page so anonymous
     * mmap is happy and the TCB ends on a fixed alignment. */
    ld_size total = align_up(cursor + LD_TLS_TCB_SIZE, 0x1000);
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
        ld_puts("ld.so: tls "); ld_puts(o->name);
        ld_puts(" memsz="); ld_putx(o->tls_memsz);
        ld_puts(" filesz="); ld_putx(o->tls_filesz);
        ld_puts(" offset=-"); ld_putx(o->tls_offset);
        ld_puts("\n");
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
    ld_puts("ld.so: TLS installed, tp="); ld_putx(tp); ld_puts("\n");
    return 0;
}
