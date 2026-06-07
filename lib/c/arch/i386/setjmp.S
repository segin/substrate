/*
 * setjmp / longjmp for i386
 *
 * jmp_buf layout (array of 6 ints):
 *   [0] ebx
 *   [1] esi
 *   [2] edi
 *   [3] ebp
 *   [4] esp (after the setjmp call frame)
 *   [5] eip (return address into caller)
 */

.globl setjmp
.type setjmp, @function
setjmp:
	movl 4(%esp), %eax      /* eax = jmp_buf ptr */
	movl %ebx,  0(%eax)
	movl %esi,  4(%eax)
	movl %edi,  8(%eax)
	movl %ebp, 12(%eax)
	movl %esp, 16(%eax)     /* save caller's esp */
	movl (%esp), %ecx       /* return address */
	movl %ecx, 20(%eax)     /* save eip */
	xorl %eax, %eax
	ret

.globl longjmp
.type longjmp, @function
longjmp:
	movl 4(%esp), %eax      /* eax = jmp_buf ptr */
	movl 8(%esp), %ecx      /* ecx = val */
	testl %ecx, %ecx
	jnz 1f
	incl %ecx               /* val == 0 becomes 1 */
1:
	movl  0(%eax), %ebx
	movl  4(%eax), %esi
	movl  8(%eax), %edi
	movl 12(%eax), %ebp
	movl 16(%eax), %esp
	movl 20(%eax), %edx
	movl %ecx, %eax         /* return value */
	jmp *%edx

/*
 * sigsetjmp(sigjmp_buf env, int savemask)
 *
 * Like setjmp this MUST save the CALLER's frame, so it is asm rather than a C
 * wrapper around setjmp (which would record the wrapper's own — soon-dead —
 * frame and crash on siglongjmp).  Save exactly what setjmp saves into
 * env->__env (offsets 0..20, same convention so longjmp/siglongjmp treat it
 * identically), then tail-jump to the hidden __sigsetjmp_mask helper, which
 * fills in the signal-mask slots (env+24/+28) and returns 0.  The tail jump
 * preserves the stack frame, so the helper's `ret` returns 0 to sigsetjmp's
 * caller; a later siglongjmp restores __env, i.e. the caller's frame.
 */
.globl sigsetjmp
.hidden __sigsetjmp_mask
.type sigsetjmp, @function
sigsetjmp:
	movl 4(%esp), %eax      /* eax = env (&__env) */
	movl %ebx,  0(%eax)
	movl %esi,  4(%eax)
	movl %edi,  8(%eax)
	movl %ebp, 12(%eax)
	movl %esp, 16(%eax)     /* caller's esp (same convention as setjmp) */
	movl (%esp), %ecx       /* caller's return eip */
	movl %ecx, 20(%eax)
	jmp __sigsetjmp_mask    /* fill mask slots, return 0 to our caller */
	.size sigsetjmp, . - sigsetjmp

/* No executable stack required — see lib/c/arch/i386/crt0.S. */
	.section .note.GNU-stack,"",@progbits
