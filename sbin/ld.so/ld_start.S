/*
 * ld_start.S — entry point for /sbin/ld.so (Substrate dynamic linker).
 *
 * Phase-2 responsibilities (executed BEFORE any C code):
 *   1. Save the kernel-handed initial %esp (where argc lives).
 *   2. Compute our own load base = runtime address of __ehdr_start.
 *   3. Walk our own PT_DYNAMIC for DT_REL / DT_RELSZ.
 *   4. Apply every R_386_RELATIVE in our image: *(base+r_offset) += base.
 *      (Only RELATIVE — we have no DT_NEEDED, no symbol resolutions.)
 *   5. Call ld_main(initial_stack), receive program entry in %eax.
 *   6. Restore %esp to the saved value, zero %ebp/%edx, jump to entry.
 *
 * After step 4 globals are reachable by their final addresses, so
 * the C code in ld_main.c is free to use string literals, function
 * pointers, etc. — anything the linker emitted as R_386_RELATIVE
 * has been fixed up.
 */

#define DT_NULL        0
#define DT_REL        17
#define DT_RELSZ      18
#define R_386_RELATIVE 8

    .text
    .global _start
    .type _start, @function

_start:
    movl  %esp, %ebp                /* save kernel-handed stack -> %ebp */

    /* --- Compute our load base --------------------------------------
     * __ehdr_start is the linker-defined symbol at the very start
     * of the loaded image (vaddr 0 in the .so).  At runtime it lives
     * at base+0, so its address IS the base. */
    call  .Lpc
.Lpc:
    popl  %ebx                      /* %ebx = runtime addr of .Lpc */
    leal  __ehdr_start - .Lpc(%ebx), %ebx  /* %ebx = base */

    /* --- Locate _DYNAMIC --------------------------------------------
     * _DYNAMIC has link-time vaddr; runtime addr = base + that. */
    leal  _DYNAMIC@GOTOFF, %ecx     /* would need GOT — easier path: */
    /* Fallback: call/pop to get a known PC address, then add the
     * fixed offset from .Lpc to _DYNAMIC at link time. */

    /* The above @GOTOFF path needs a GOT register set up; easier
     * pattern: re-anchor at .Lpc and use _DYNAMIC - .Lpc as a
     * link-time-known offset. */
    call  .Lpc2
.Lpc2:
    popl  %ecx                      /* %ecx = runtime addr of .Lpc2 */
    leal  _DYNAMIC - .Lpc2(%ecx), %ecx  /* %ecx = runtime &_DYNAMIC */

    /* --- Walk dynamic table for DT_REL and DT_RELSZ ----------------
     * Save: %ecx = current Elf32_Dyn*
     * Out:  %edi = REL table base (runtime addr), %esi = REL bytes */
    xorl  %edi, %edi
    xorl  %esi, %esi
.Ldyn_loop:
    movl  (%ecx), %eax              /* d_tag */
    testl %eax, %eax
    jz    .Ldyn_done
    cmpl  $DT_REL, %eax
    je    .Ldyn_set_rel
    cmpl  $DT_RELSZ, %eax
    je    .Ldyn_set_relsz
    addl  $8, %ecx
    jmp   .Ldyn_loop
.Ldyn_set_rel:
    movl  4(%ecx), %edi
    addl  %ebx, %edi                /* %edi = base + d_un.d_ptr */
    addl  $8, %ecx
    jmp   .Ldyn_loop
.Ldyn_set_relsz:
    movl  4(%ecx), %esi             /* %esi = d_un.d_val (bytes) */
    addl  $8, %ecx
    jmp   .Ldyn_loop
.Ldyn_done:

    /* --- Apply R_386_RELATIVE entries ------------------------------
     * Each Elf32_Rel is { r_offset, r_info } = 8 bytes.
     * If type == R_386_RELATIVE: *(base + r_offset) += base. */
    testl %edi, %edi
    jz    .Lreloc_done
.Lrel_loop:
    testl %esi, %esi
    jz    .Lreloc_done
    movl  (%edi), %eax              /* r_offset */
    movl  4(%edi), %edx             /* r_info */
    movzbl %dl, %edx                /* type = r_info & 0xff */
    cmpl  $R_386_RELATIVE, %edx
    jne   .Lrel_next
    addl  %ebx, %eax                /* %eax = base + r_offset */
    addl  %ebx, (%eax)              /* *p += base */
.Lrel_next:
    addl  $8, %edi
    subl  $8, %esi
    jmp   .Lrel_loop
.Lreloc_done:

    /* --- Hand off to C ---------------------------------------------
     * Cdecl: push args right-to-left, call.  Single arg = the
     * saved kernel-handed stack pointer. */
    pushl %ebp
    call  ld_main
    addl  $4, %esp                  /* drop our pushed arg */

    /* --- Jump to the program entry ---------------------------------
     * %eax = program entry returned by ld_main.  Restore %esp to
     * the kernel-handed value and clear %ebp / %edx (the latter is
     * the atexit pointer slot; native ABI expects it zero). */
    movl  %ebp, %esp
    xorl  %ebp, %ebp
    xorl  %edx, %edx
    jmp   *%eax

    .size _start, . - _start
