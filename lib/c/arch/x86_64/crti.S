/*
 * crti.S — init/fini section prologue, amd64.  See arch/i386/crti.S for
 * the layered-link picture; this is the same sandwich with a 64-bit frame.
 */

	.section .init,"ax",@progbits
	.globl _init
	.type _init, @function
_init:
	pushq	%rbp
	movq	%rsp, %rbp
	/* crtbegin.o and the .init body for every other linked
	 * object are appended here by ld at link time. */

	.section .fini,"ax",@progbits
	.globl _fini
	.type _fini, @function
_fini:
	pushq	%rbp
	movq	%rsp, %rbp
	/* crtbegin.o and the .fini body follow. */

	.section .note.GNU-stack,"",@progbits
