/*
 * crt0.S — C runtime startup for Substrate native userland.
 *
 * Both static and dynamic builds link this object.  For static
 * binaries the relocations resolve at link time; for PIE builds
 * driven by /sbin/ld.so the GOT slots get filled in by ld.so's
 * relocation pass before _start runs.
 *
 * Stack layout at entry (kernel-built):
 *   [esp+0]   = argc
 *   [esp+4..] = argv[0..argc-1], NULL, envp[0..N-1], NULL, auxv...
 *
 * The native ABI clears %edx, %ebp at exec; whatever the kernel
 * leaves in callee-saved regs is what we get.  PIE-built code
 * additionally expects %ebx to be set up by us to point at the
 * GOT before any @PLT call or @GOT data access.
 */

    .global _start
    .extern main
    .extern exit
    .extern environ
    .extern __stdio_init

    .text
_start:
    /* argc / argv / envp */
    movl (%esp), %eax           /* argc */
    leal 4(%esp), %ecx           /* argv */
    movl %eax, %edx
    incl %edx
    shll $2, %edx
    leal (%ecx, %edx), %edx      /* envp = argv + (argc+1)*4 */

    /* Stash argc/argv/envp for the call to main(); push in reverse
     * order so [esp+0]=argc, [esp+4]=argv, [esp+8]=envp at the
     * call site.  The push of envp happens AFTER we set environ. */

    /* Set up %ebx as the GOT pointer for PIC accesses below. */
    call .Lget_pc
.Lget_pc:
    popl %ebx
    addl $_GLOBAL_OFFSET_TABLE_+[.-.Lget_pc], %ebx

    /* environ = envp.   Use @GOT so the linker emits a GOT slot
     * that ld.so will fill with the runtime address of `environ`. */
    movl environ@GOT(%ebx), %edi
    movl %edx, (%edi)

    /* Save argc/argv/envp across the stdio init call. */
    pushl %edx                   /* envp */
    pushl %ecx                   /* argv */
    pushl %eax                   /* argc */
    call __stdio_init@PLT

    /* main(argc, argv, envp) — args still on stack. */
    call main@PLT

    /* main returned in %eax — discard the three pushed args and
     * pass the value to exit(). */
    addl $12, %esp
    pushl %eax
    call exit@PLT

    /* Should not reach here. */
1:  hlt
    jmp 1b

    .size _start, . - _start

/* Tell GNU ld the resulting executable does not need an executable
 * stack.  Without this section, ld defaults to a W+X stack and warns
 * "missing .note.GNU-stack section implies executable stack" at
 * every link that consumes this object. */
    .section .note.GNU-stack,"",@progbits
