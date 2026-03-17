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
