/*
 * crtn.S — init/fini section epilogue, amd64.  Companion to crti.S.
 */

	.section .init,"ax",@progbits
	popq	%rbp
	ret

	.section .fini,"ax",@progbits
	popq	%rbp
	ret

	.section .note.GNU-stack,"",@progbits
