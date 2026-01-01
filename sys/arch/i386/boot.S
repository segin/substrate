#define ASM_FILE 1
#include "multiboot.h"

# Multiboot Header
.section .multiboot
.align 4
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_HEADER_FLAGS
.long -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

# Stack
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

# Entry point
.section .text
.global _start
.type _start, @function
_start:
	# Set up stack
	mov $stack_top, %esp

	# Reset EFLAGS
	push $0
	popf

	# Push Multiboot Info
	push %ebx
	push %eax

	# Call kernel
	call kmain

	# Hang
	cli
1:	hlt
	jmp 1b
