/*
 * ld_plt_i386.S - the lazy-binding trampoline of /sbin/ld.so.
 *
 * A lazily bound object's PLT0 does `pushl GOT[1]; jmp *GOT[2]`, and
 * ld_reloc_lazy_setup() (ld_reloc.c) put the object's descriptor in
 * GOT[1] and this routine in GOT[2].  The PLT stub that jumped to PLT0
 * had already pushed the byte offset of its entry in DT_JMPREL, so we
 * arrive in the middle of somebody's function call with
 *
 *      0(%esp)   the object           (pushed by PLT0)
 *      4(%esp)   the DT_JMPREL offset (pushed by the PLT stub)
 *      8(%esp)   the caller's return address
 *
 * ld_plt_fixup(obj, offset) binds the slot and returns the function; we
 * then leave the stack as the caller built it and jump there.
 *
 * The arguments of an ordinary call are on the stack and need nothing.
 * What has to survive is %eax, %edx and %ecx, which carry the arguments
 * of a regparm or fastcall function - ___tls_get_addr takes its one in
 * %eax.  The linker is built without x87 or SSE code; a resolver of an
 * indirect function, which the fixup may call, is expected to leave the
 * vector registers alone (they carry arguments only for sseregparm
 * functions, which nothing here uses).
 *
 * %esp is 12 mod 16 when the PLT stub is entered; the two words above
 * and the three registers make it 8, and the two arguments 0, which is
 * the alignment the psABI wants at the call.
 */

    .text
    .global ld_plt_trampoline
    .hidden ld_plt_trampoline
    .hidden ld_plt_fixup
    .type ld_plt_trampoline, @function

ld_plt_trampoline:
    pushl %eax
    pushl %ecx
    pushl %edx
    movl  12(%esp), %eax            /* the object */
    movl  16(%esp), %edx            /* the DT_JMPREL offset */
    pushl %edx
    pushl %eax
    call  ld_plt_fixup
    addl  $8, %esp
    movl  %eax, 16(%esp)            /* the function, over the offset */
    popl  %edx
    popl  %ecx
    popl  %eax
    addl  $4, %esp                  /* the object */
    ret                             /* to the function; the caller's
                                     * return address is now on top */

    .size ld_plt_trampoline, . - ld_plt_trampoline

    .section .note.GNU-stack,"",@progbits
