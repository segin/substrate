/*
 * crti.S — init/fini section prologue.
 *
 * Together with crtn.S this provides the conventional ELF .init /
 * .fini section sandwich:
 *
 *      crti.o : prologue   (function header — push %ebp etc.)
 *  +   crtbegin.o : middle (ctor/dtor list head)
 *  +   <translation units' .init/.fini bodies>
 *  +   crtend.o : middle   (ctor/dtor list tail)
 *  +   crtn.o : epilogue   (leave; ret)
 *
 * The resulting _init / _fini symbols are what DT_INIT / DT_FINI
 * point at, and what the dynamic linker calls before main() and
 * after exit() respectively.  Substrate's own ld.so currently
 * walks DT_INIT_ARRAY / DT_FINI_ARRAY for static-init and global
 * destructors, so these functions can stay empty bodies — but
 * having them present is what lets gcc-generated binaries link
 * cleanly against a STARTFILE_SPEC that names crti.o.
 *
 * No relocations needed; this is leaf assembly.  The same object
 * is used in both static and shared (PIC) links — no GOT setup,
 * no @PLT calls.
 */

	.section .init,"ax",@progbits
	.globl _init
	.type _init, @function
_init:
	pushl	%ebp
	movl	%esp, %ebp
	/* crtbegin.o and the .init body for every other linked
	 * object are appended here by ld at link time. */

	.section .fini,"ax",@progbits
	.globl _fini
	.type _fini, @function
_fini:
	pushl	%ebp
	movl	%esp, %ebp
	/* crtbegin.o and the .fini body follow. */

	.section .note.GNU-stack,"",@progbits
