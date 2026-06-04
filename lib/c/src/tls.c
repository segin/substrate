/*
 * tls.c — GD/LD dynamic-TLS resolver entry points.
 *
 * The general-/local-dynamic TLS models compile to a call to __tls_get_addr
 * (stack arg) or, on i386, ___tls_get_addr (tls_index pointer in %eax,
 * regparm(1)).  The helper returns the address, in the current thread, of the
 * TLS variable named by the tls_index { module-id, offset-within-module }.
 *
 * libc defines these (rather than ld.so alone) so that a program or shared
 * object using GD/LD TLS resolves them at LINK time against libc — which every
 * dynamic binary DT_NEEDEDs — instead of failing with "undefined reference to
 * ___tls_get_addr"; ld.so is the interpreter, not a link-time library.
 *
 * The lookup is self-contained: the dynamic linker publishes a Dynamic Thread
 * Vector at TCB[1] (gs:4), where DTV[module-id] is the base of that module's
 * TLS block in the current thread.  So the address is DTV[ti_module] +
 * ti_offset — no call back into ld.so.
 */

typedef struct {
    unsigned long ti_module;
    unsigned long ti_offset;
} tls_index;

/* The per-thread DTV pointer lives at TCB[1] = gs:4 (gs:0 is the TCB
 * self-pointer of the variant-II layout). */
static inline unsigned long *tls_dtv(void)
{
    unsigned long *dtv;
    __asm__("movl %%gs:4, %0" : "=r"(dtv));
    return dtv;
}

void *__tls_get_addr(tls_index *ti)
{
    return (void *)(tls_dtv()[ti->ti_module] + ti->ti_offset);
}

/* i386 ABI: argument arrives in %eax (regparm(1)); result returned in %eax. */
__attribute__((regparm(1))) void *___tls_get_addr(tls_index *ti)
{
    return (void *)(tls_dtv()[ti->ti_module] + ti->ti_offset);
}
