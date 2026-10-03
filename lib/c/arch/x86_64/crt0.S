/*
 * crt0.S — C runtime startup for Substrate native userland, amd64.
 *
 * Both static and dynamic builds link this object; every reference is
 * %rip-relative through the GOT or PLT, so it serves non-PIE and PIE
 * links alike.
 *
 * At entry (docs/specs/abi-amd64.md, section 6) %rsp points at argc and
 * is 16-byte aligned:
 *   [rsp+0]   = argc
 *   [rsp+8..] = argv[0..argc-1], NULL, envp[0..N-1], NULL, auxv...
 */

    .global _start
    .extern main
    .extern exit
    .extern environ
    .extern __stdio_init
    .extern __init_stack_guard

    .text
    .type _start, @function
_start:
    xorl %ebp, %ebp              /* outermost frame */

    /* argc / argv / envp, kept in callee-saved registers across the
     * initialisation calls. */
    movq (%rsp), %r12            /* argc */
    leaq 8(%rsp), %r13           /* argv */
    leaq 8(%r13,%r12,8), %r14    /* envp = argv + (argc+1)*8 */

    /* environ = envp */
    movq environ@GOTPCREL(%rip), %rax
    movq %r14, (%rax)

    /* Seed the stack-protector canary from the AT_RANDOM auxv entry
     * BEFORE any stack-protected C function runs. */
    movq %r14, %rdi
    call __init_stack_guard@PLT

    /* Thread-local storage for a statically linked program (a no-op when
     * the dynamic linker has already set it up). */
    movq %r14, %rdi
    call __libc_static_tls_init@PLT

    call __stdio_init@PLT

    /* main(argc, argv, envp) */
    movl %r12d, %edi
    movq %r13, %rsi
    movq %r14, %rdx
    call main@PLT

    movl %eax, %edi
    call exit@PLT

    /* Should not reach here. */
1:  hlt
    jmp 1b
    .size _start, . - _start

/* No executable stack required. */
    .section .note.GNU-stack,"",@progbits
