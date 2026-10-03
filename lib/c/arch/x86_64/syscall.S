/*
 * Substrate native system-call stubs, amd64.
 *
 *   int64_t _syscallN(int num, uintptr_t a1, ..., uintptr_t aN);
 *
 * The native amd64 ABI (docs/specs/abi-amd64.md, section 3): number in
 * %rax, arguments in %rdi %rsi %rdx %r10 %r8 %r9.  The C caller passes the
 * number in %rdi and the arguments one register along, with a sixth on the
 * stack, so every arity is the same shift and the stubs share one body --
 * the registers a shorter call leaves unset carry values the kernel never
 * reads.
 *
 * The kernel signals an error with the carry flag and the positive errno
 * in %rax.  The stubs return -errno, as the i386 ones do, so the C code
 * above them is the same on both architectures.  A 64-bit result needs no
 * separate entry here: _syscall4_ll is _syscall4.
 */

.global _syscall0
.global _syscall1
.global _syscall2
.global _syscall3
.global _syscall4
.global _syscall4_ll
.global _syscall5
.global _syscall6
.type _syscall0, @function
.type _syscall1, @function
.type _syscall2, @function
.type _syscall3, @function
.type _syscall4, @function
.type _syscall4_ll, @function
.type _syscall5, @function
.type _syscall6, @function

_syscall0:
_syscall1:
_syscall2:
_syscall3:
_syscall4:
_syscall4_ll:
_syscall5:
_syscall6:
    movslq %edi, %rax       /* number */
    movq  %rsi, %rdi
    movq  %rdx, %rsi
    movq  %rcx, %rdx
    movq  %r8,  %r10
    movq  %r9,  %r8
    movq  8(%rsp), %r9      /* a6; above the return address */
    syscall
    jnc   1f
    negq  %rax              /* error: -errno */
1:  ret

.size _syscall6, . - _syscall6

    .section .note.GNU-stack,"",@progbits
