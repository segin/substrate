/*
 * setjmp / longjmp for amd64
 *
 * jmp_buf layout (array of 8 longs, <setjmp.h>): the registers the psABI
 * makes callee-saved, then the stack pointer and return address.
 *   [0] rbx   [1] rbp   [2] r12   [3] r13
 *   [4] r14   [5] r15
 *   [6] rsp (as it is after setjmp returns)
 *   [7] rip (return address into caller)
 *
 * _setjmp / _longjmp are the non-signal-mask variants; setjmp never
 * touches the signal mask here (sigsetjmp is the masking one), so they are
 * exact aliases.
 */

.globl _setjmp
.type _setjmp, @function
.set _setjmp, setjmp
.globl _longjmp
.type _longjmp, @function
.set _longjmp, longjmp

.globl setjmp
.type setjmp, @function
setjmp:
	movq %rbx,  0(%rdi)
	movq %rbp,  8(%rdi)
	movq %r12, 16(%rdi)
	movq %r13, 24(%rdi)
	movq %r14, 32(%rdi)
	movq %r15, 40(%rdi)
	leaq 8(%rsp), %rax      /* caller's rsp once we have returned */
	movq %rax, 48(%rdi)
	movq (%rsp), %rax       /* return address */
	movq %rax, 56(%rdi)
	xorl %eax, %eax
	ret
	.size setjmp, . - setjmp

.globl longjmp
.type longjmp, @function
longjmp:
	movl %esi, %eax         /* return value */
	testl %eax, %eax
	jnz 1f
	incl %eax               /* val == 0 becomes 1 */
1:
	movq  0(%rdi), %rbx
	movq  8(%rdi), %rbp
	movq 16(%rdi), %r12
	movq 24(%rdi), %r13
	movq 32(%rdi), %r14
	movq 40(%rdi), %r15
	movq 48(%rdi), %rsp
	jmp *56(%rdi)
	.size longjmp, . - longjmp

/*
 * sigsetjmp(sigjmp_buf env, int savemask)
 *
 * Saves the CALLER's frame into env->__env exactly as setjmp does (it is
 * the first member, so env is its address), then tail-jumps to the hidden
 * __sigsetjmp_mask helper with both arguments still in place; the helper
 * fills in the signal-mask slots and returns 0 to sigsetjmp's caller.
 */
.globl sigsetjmp
.hidden __sigsetjmp_mask
.type sigsetjmp, @function
sigsetjmp:
	movq %rbx,  0(%rdi)
	movq %rbp,  8(%rdi)
	movq %r12, 16(%rdi)
	movq %r13, 24(%rdi)
	movq %r14, 32(%rdi)
	movq %r15, 40(%rdi)
	leaq 8(%rsp), %rax
	movq %rax, 48(%rdi)
	movq (%rsp), %rax
	movq %rax, 56(%rdi)
	jmp __sigsetjmp_mask
	.size sigsetjmp, . - sigsetjmp

	.section .note.GNU-stack,"",@progbits
