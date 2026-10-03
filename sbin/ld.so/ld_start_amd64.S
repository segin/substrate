/*
 * ld_start_amd64.S - entry point for /sbin/ld64.so, the 64-bit Substrate
 * dynamic linker.  The amd64 counterpart of ld_start.S.
 *
 * At entry (docs/specs/abi-amd64.md, section 6) %rsp points at argc and
 * is 16-byte aligned; the stack holds 8-byte words.
 *
 * Executed BEFORE any C code:
 *   1. Save the kernel-handed initial %rsp (where argc lives).
 *   2. Compute our own load base = runtime address of __ehdr_start.
 *   3. Walk our own _DYNAMIC for DT_RELA / DT_RELASZ.
 *   4. Apply every R_X86_64_RELATIVE in our image:
 *        *(base + r_offset) = base + r_addend.
 *      (Only RELATIVE - we have no DT_NEEDED, no symbol resolutions.)
 *   5. Call ld_main(initial_stack), receive the program entry in %rax.
 *   6. Restore %rsp, set %rdi to the same address and %rdx to 0 - the
 *      registers the kernel would have handed the program itself - and
 *      jump to the entry.
 *
 * Both symbols are reached %rip-relative, which needs no relocation; they
 * are marked hidden so the link editor accepts that in a shared object.
 */

#define DT_NULL            0
#define DT_RELA            7
#define DT_RELASZ          8
#define R_X86_64_RELATIVE  8

#define DYN_SIZE    16      /* Elf64_Dyn: d_tag, d_un */
#define RELA_SIZE   24      /* Elf64_Rela: r_offset, r_info, r_addend */

    .text
    .hidden __ehdr_start
    .hidden _DYNAMIC
    .global _start
    .type _start, @function

_start:
    movq  %rsp, %r12                /* kernel-handed stack; callee-saved */

    /* __ehdr_start is the linker-defined symbol at the very start of the
     * loaded image (vaddr 0 in the .so), so its address IS the base. */
    leaq  __ehdr_start(%rip), %rbx  /* %rbx = base */
    leaq  _DYNAMIC(%rip), %rcx      /* %rcx = runtime &_DYNAMIC */

    /* --- Walk the dynamic table for DT_RELA and DT_RELASZ -----------
     * Out: %rdi = RELA table (runtime address), %rsi = its size. */
    xorl  %edi, %edi
    xorl  %esi, %esi
.Ldyn_loop:
    movq  (%rcx), %rax              /* d_tag */
    testq %rax, %rax
    jz    .Ldyn_done
    cmpq  $DT_RELA, %rax
    je    .Ldyn_set_rela
    cmpq  $DT_RELASZ, %rax
    je    .Ldyn_set_relasz
.Ldyn_next:
    addq  $DYN_SIZE, %rcx
    jmp   .Ldyn_loop
.Ldyn_set_rela:
    movq  8(%rcx), %rdi
    addq  %rbx, %rdi                /* %rdi = base + d_un.d_ptr */
    jmp   .Ldyn_next
.Ldyn_set_relasz:
    movq  8(%rcx), %rsi             /* %rsi = d_un.d_val (bytes) */
    jmp   .Ldyn_next
.Ldyn_done:

    /* --- Apply the R_X86_64_RELATIVE entries ----------------------- */
    testq %rdi, %rdi
    jz    .Lreloc_done
.Lrela_loop:
    cmpq  $RELA_SIZE, %rsi
    jb    .Lreloc_done
    movl  8(%rdi), %eax             /* type = low 32 bits of r_info */
    cmpl  $R_X86_64_RELATIVE, %eax
    jne   .Lrela_next
    movq  (%rdi), %rax              /* r_offset */
    movq  16(%rdi), %rdx            /* r_addend */
    addq  %rbx, %rdx                /* base + r_addend */
    movq  %rdx, (%rbx,%rax)         /* *(base + r_offset) = ... */
.Lrela_next:
    addq  $RELA_SIZE, %rdi
    subq  $RELA_SIZE, %rsi
    jmp   .Lrela_loop
.Lreloc_done:

    /* --- Hand off to C ---------------------------------------------
     * %rsp is 16-byte aligned here, as the psABI requires at a call. */
    movq  %r12, %rdi
    call  ld_main

    /* --- Jump to the program entry ---------------------------------
     * %rax = program entry returned by ld_main. */
    movq  %r12, %rsp
    movq  %r12, %rdi
    xorl  %edx, %edx
    xorl  %ebp, %ebp
    jmp   *%rax

    .size _start, . - _start

    .section .note.GNU-stack,"",@progbits
