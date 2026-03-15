#define ASM_FILE 1
#include "multiboot.h"

# Multiboot Header
.section .multiboot
.align 4
    # Entry point for Linux zImage loader (jumps to _start)
    # This allows the kernel to be loaded at 0x100000 and executed from offset 0
    jmp _start
    .align 4
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_HEADER_FLAGS
.long -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)
.long 0         # mode_type: 0 = linear graphics
.long 1024      # width
.long 768       # height
.long 32        # depth

.section .setup, "ax"
.global _start
.type _start, @function
_start:
	# Note: We are loaded at 1MB physical. 
	cli

	# Save Multiboot magic and info pointer
	mov %eax, %ebp

	# 1. Setup Page Tables to map first 16MB
	# Tables map physical 0..16MB to 0..16MB (Identity) and
	# 3GB..3GB+16MB (Higher Half)

	# Table 1: 0..4MB
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

	# Table 2: 4..8MB
	mov $(boot_page_table2 - 0xC0000000), %edi
	mov $1024, %esi # Start at page 1024 (4MB)
	mov $1024, %ecx
2:
	mov %esi, %edx
	shl $12, %edx
	or $3, %edx  # Present + RW
	mov %edx, (%edi)
	add $4, %edi
	inc %esi
	loop 2b

	# Table 3: 8..12MB
	mov $(boot_page_table3 - 0xC0000000), %edi
	mov $2048, %esi # Start at page 2048 (8MB)
	mov $1024, %ecx
3:
	mov %esi, %edx
	shl $12, %edx
	or $3, %edx  # Present + RW
	mov %edx, (%edi)
	add $4, %edi
	inc %esi
	loop 3b

	# Table 4: 12..16MB
	mov $(boot_page_table4 - 0xC0000000), %edi
	mov $3072, %esi # Start at page 3072 (12MB)
	mov $1024, %ecx
4:
	mov %esi, %edx
	shl $12, %edx
	or $3, %edx  # Present + RW
	mov %edx, (%edi)
	add $4, %edi
	inc %esi
	loop 4b

	# 2. Setup Page Directory
	mov $(boot_page_directory - 0xC0000000), %edx

	# Entry 0, 1, 2, 3: PT1, PT2, PT3, PT4 (Identity 0..16MB)
	mov $(boot_page_table1 - 0xC0000000), %eax
	or $3, %eax
	mov %eax, (%edx)
	mov $(boot_page_table2 - 0xC0000000), %eax
	or $3, %eax
	mov %eax, 4(%edx)
	mov $(boot_page_table3 - 0xC0000000), %eax
	or $3, %eax
	mov %eax, 8(%edx)
	mov $(boot_page_table4 - 0xC0000000), %eax
	or $3, %eax
	mov %eax, 12(%edx)

	# Entry 768, 769, 770, 771: PT1..PT4 (Higher Half 3GB..3GB+16MB)
	mov $(boot_page_table1 - 0xC0000000), %eax
	or $3, %eax
	mov %eax, 3072(%edx)
	mov $(boot_page_table2 - 0xC0000000), %eax
	or $3, %eax
	mov %eax, 3076(%edx)
	mov $(boot_page_table3 - 0xC0000000), %eax
	or $3, %eax
	mov %eax, 3080(%edx)
	mov $(boot_page_table4 - 0xC0000000), %eax
	or $3, %eax
	mov %eax, 3084(%edx)

	# Entry 1019 (0x3FB*4=4076): LAPIC page table (0xFEC00000-0xFFFFF000)
	# This maps the Local APIC at 0xFEE00000
	mov $(boot_page_table_lapic - 0xC0000000), %eax
	or $3, %eax
	mov %eax, 4076(%edx)

	# Fill LAPIC page table - identity map 0xFEC00000-0xFFFFFFFF
	mov $(boot_page_table_lapic - 0xC0000000), %edi
	mov $0xFEC00, %esi          # Start at page 0xFEC00 (0xFEC00000 >> 12)
	mov $1024, %ecx
3:
	mov %esi, %edx
	shl $12, %edx
	or $0x13, %edx              # Present + RW + PCD (no-cache for MMIO)
	mov %edx, (%edi)
	add $4, %edi
	inc %esi
	loop 3b
	
	# Restore page directory pointer
	mov $(boot_page_directory - 0xC0000000), %edx

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
	movl $0, (boot_page_directory + 4)
	movl $0, (boot_page_directory + 8)
	movl $0, (boot_page_directory + 12)
	
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
boot_page_table2:
	.skip 4096
boot_page_table3:
	.skip 4096
boot_page_table4:
	.skip 4096
boot_page_table_lapic:
	.skip 4096
.align 16
stack_bottom:
	.skip 16384
.global stack_top
stack_top:
