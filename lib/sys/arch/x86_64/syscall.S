/*
 * syscall.S - amd64 raw syscall entry point (native personality ABI)
 *
 *   long syscall(long nr, ...);
 *
 * The native amd64 ABI (docs/specs/abi-amd64.md, section 3) takes the
 * number in %rax and the arguments in %rdi %rsi %rdx %r10 %r8 %r9, with a
 * seventh and eighth read from 8(%rsp) and 16(%rsp).  The C caller has the
 * number in %rdi and the arguments one register along, so shift them down.
 * The caller's seventh argument (the call's sixth) is at 8(%rsp) and its
 * eighth and ninth at 16(%rsp) and 24(%rsp); the latter two are copied
 * down so the kernel finds them where it looks.
 *
 * The kernel reports failure with the carry flag set and the positive
 * errno in %rax.  Like the i386 entry, this returns -errno instead, which
 * is what every C wrapper above it (__sysret) expects.
 */

.global syscall
.type syscall, @function

syscall:
    movq  %rdi, %rax        /* number */
    movq  %rsi, %rdi
    movq  %rdx, %rsi
    movq  %rcx, %rdx
    movq  %r8,  %r10
    movq  %r9,  %r8
    movq  8(%rsp), %r9      /* sixth argument */
    subq  $24, %rsp         /* [0] ignored, [8] and [16]: arguments 7, 8 */
    movq  40(%rsp), %rcx
    movq  %rcx, 8(%rsp)
    movq  48(%rsp), %rcx
    movq  %rcx, 16(%rsp)
    syscall
    jnc   1f
    negq  %rax              /* error: -errno */
1:  addq  $24, %rsp
    ret

.size syscall, . - syscall

    .section .note.GNU-stack,"",@progbits
