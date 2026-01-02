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

	# Check for Multiboot magic
	cmp $MULTIBOOT_BOOTLOADER_MAGIC, %eax
	je .is_multiboot

	# Check for FreeBSD magic (0x12345678)
	cmp $0x12345678, %eax
	je .is_freebsd

	# Unknown bootloader, still pass args
	jmp .is_multiboot

.is_freebsd:
	# Convert FreeBSD bootinfo to something kmain understands or tag it
	# For now, just pass them as is.
	# We can use a different magic to let kmain know
	mov $0xF8EEB5D0, %eax # Internal tag for FreeBSD boot
	jmp .is_multiboot

.is_multiboot:
	# Push Multiboot Info
	push %ebx
	push %eax

	# Call kernel
	call kmain

	# Hang
	cli
1:	hlt
	jmp 1b
