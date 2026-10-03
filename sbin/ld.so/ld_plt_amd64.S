/*
 * ld_plt_amd64.S - the lazy-binding trampoline of /sbin/ld64.so.
 *
 * A lazily bound object's PLT0 does `push GOT[1]; jmp *GOT[2]`, and
 * ld_reloc_lazy_setup() (ld_reloc_amd64.c) put the object's descriptor
 * in GOT[1] and this routine in GOT[2].  The PLT stub that jumped to
 * PLT0 had already pushed the index of its DT_JMPREL entry, so we arrive
 * in the middle of somebody's function call with
 *
 *      0(%rsp)   the object           (pushed by PLT0)
 *      8(%rsp)   the DT_JMPREL index  (pushed by the PLT stub)
 *     16(%rsp)   the caller's return address
 *
 * and every argument register live.  ld_plt_fixup(obj, index) binds the
 * slot and returns the function; we then drop the two words and jump to
 * it, so the callee sees the call exactly as the caller made it.
 *
 * What has to survive: the six integer argument registers, %rax (the
 * vector-register count of a variadic call), %r10 (the static chain),
 * and the vector argument registers.  The linker itself is built with
 * -mgeneral-regs-only and never touches the latter, but the fixup may
 * call an indirect function's resolver, which is library code and may;
 * FXSAVE covers %xmm0-%xmm15 and the x87 state.  It does not cover the
 * upper halves of %ymm, so a resolver using AVX would spoil a 256-bit
 * vector argument; none of ours does.  %r11 is the psABI's scratch
 * register for exactly this path and carries the target.
 */

    .text
    .global ld_plt_trampoline
    .hidden ld_plt_trampoline
    .hidden ld_plt_fixup
    .type ld_plt_trampoline, @function

ld_plt_trampoline:
    pushq %rbp
    movq  %rsp, %rbp                /* 8(%rbp) = obj, 16(%rbp) = index */
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10                      /* -64(%rbp) */

    subq  $512, %rsp
    andq  $-16, %rsp                /* FXSAVE area, and the call below */
    fxsave (%rsp)

    movq  8(%rbp), %rdi
    movq  16(%rbp), %rsi
    call  ld_plt_fixup
    movq  %rax, %r11

    fxrstor (%rsp)
    leaq  -64(%rbp), %rsp
    popq  %r10
    popq  %r9
    popq  %r8
    popq  %rdi
    popq  %rsi
    popq  %rdx
    popq  %rcx
    popq  %rax
    popq  %rbp
    addq  $16, %rsp                 /* the object and the index */
    jmp   *%r11

    .size ld_plt_trampoline, . - ld_plt_trampoline

    .section .note.GNU-stack,"",@progbits
