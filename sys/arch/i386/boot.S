#define ASM_FILE 1
#include "multiboot.h"

# Multiboot Header
.section .multiboot
.align 4
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_HEADER_FLAGS
.long -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

.section .setup, "ax"
.global _start
.type _start, @function
_start:
	# Note: We are loaded at 1MB physical. 
	cli

	# Save Multiboot magic and info pointer
	mov %eax, %ebp

	# 1. Setup Page Table to map first 4MB to physical 0 (Identity)
	# and also map to 3GB (Higher Half)
	mov $(boot_page_table1 - 0xC0000000), %edi
	mov $0, %esi
	mov $1024, %ecx

1:
	mov %esi, %edx
	shl $12, %edx
	or $3, %edx  # Present + RW
	mov %edx, (%edi)
	add $4, %edi
	inc %esi
	loop 1b

	# 2. Setup Page Directory
	mov $(boot_page_directory - 0xC0000000), %edx
	# Entry 0: PT1 (Identity)
	mov $(boot_page_table1 - 0xC0000000), %eax
	or $3, %eax
	mov %eax, (%edx)
	# Entry 768: PT1 (Higher Half)
	mov %eax, 3072(%edx)

	# 3. Enable Paging
	mov %edx, %cr3
	mov %cr0, %eax
	or $0x80000000, %eax
	mov %eax, %cr0

	# Restore magic
	mov %ebp, %eax

	# 4. Long Jump to Higher Half
	lea higher_half, %edx
	jmp *%edx

.section .text
higher_half:
	# Now executing in virtual memory at 0xC01xxxxx
	
	# Unmap identity mapping
	movl $0, (boot_page_directory)
	
	# Flush TLB
	mov %cr3, %edx
	mov %edx, %cr3

	# Set up stack
	mov $stack_top, %esp

	# Reset EFLAGS
	push $0
	popf

	# Adjust Multiboot info pointer to virtual address 
	add $0xC0000000, %ebx

	push %ebx
	push %eax

	call kmain

	cli
1:	hlt
	jmp 1b

.section .bss
.align 4096
boot_page_directory:
	.skip 4096
boot_page_table1:
	.skip 4096
.align 16
stack_bottom:
	.skip 16384
stack_top:
