/*
 * crtn.S — init/fini section epilogue.  Companion to crti.S; see
 * that file for the full layered-link picture.
 */

	.section .init,"ax",@progbits
	leave
	ret

	.section .fini,"ax",@progbits
	leave
	ret

	.section .note.GNU-stack,"",@progbits
