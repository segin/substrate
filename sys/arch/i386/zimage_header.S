#define ASM_FILE 1

.section .zimage_header, "a"
.code16

/*
 * Minimal Linux boot-protocol setup stage.
 * Executed in 16-bit real mode by Linux loaders (e.g., LOADLIN/QEMU -kernel).
 * It switches to 32-bit protected mode, builds a tiny multiboot_info block
 * with cmdline pointer, then jumps to code32_start (kernel _start at 1MB).
 */

# Linux boot header at start of setup/boot sector
.global zimage_start
zimage_start:
    jmp setup_start

.org 0x1F1
setup_sects: .byte 0
root_flags:  .word 0
syssize:     .long 0
ram_size:    .word 0
vid_mode:    .word 0
root_dev:    .word 0
boot_flag:   .word 0xAA55

# Header start at 0x202
.org 0x202
header_magic: .ascii "HdrS"
header_version: .word 0x0203 # 2.03
realmode_swtch: .long 0
start_sys_seg:  .word 0x1000
kernel_version: .word 0
type_of_loader: .byte 0xFF
loadflags:      .byte 1 # Loaded high
setup_move_size: .word 0
code32_start:   .long 0x100000 # Jump to 1MB
ramdisk_image:  .long 0
ramdisk_size:   .long 0
bootsect_kludge: .long 0
heap_end_ptr:   .word 0
pad1:           .word 0
cmd_line_ptr:   .long 0
initrd_addr_max: .long 0x37FFFFFF

setup_start:
    cli
    cld

    # Enable A20 via fast A20 gate (port 0x92)
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    lgdt gdt_desc

    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0

    ljmp $0x08, $pm_entry

.code32
pm_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $SETUP_STACK_TOP, %esp

    # Build minimal multiboot info (cmdline + optional initrd as module 0).
    movl $MBI_ADDR, %edi
    xorl %eax, %eax
    movl $32, %ecx                 # 32 dwords = 128 bytes
    rep stosl

    movl $0x00000004, %eax         # MULTIBOOT_INFO_CMDLINE
    movl $MBI_ADDR, %edi
    movl %eax, 0(%edi)

    movl cmd_line_ptr, %eax        # Filled by loader using Linux boot ABI
    movl %eax, 16(%edi)            # multiboot_info_t.cmdline

    # If Linux loader provided initrd, expose it as one Multiboot module.
    movl ramdisk_size, %ecx
    testl %ecx, %ecx
    jz 1f
    movl ramdisk_image, %edx
    testl %edx, %edx
    jz 1f

    movl $0x0000000C, 0(%edi)      # MULTIBOOT_INFO_CMDLINE | MULTIBOOT_INFO_MODS
    movl $1, 20(%edi)              # multiboot_info_t.mods_count
    movl $MBI_MOD_ADDR, 24(%edi)   # multiboot_info_t.mods_addr
    movl %edx, MBI_MOD_ADDR        # module[0].mod_start
    addl %ecx, %edx
    movl %edx, MBI_MOD_ADDR + 4    # module[0].mod_end
    movl $0, MBI_MOD_ADDR + 8      # module[0].cmdline
    movl $0, MBI_MOD_ADDR + 12     # module[0].pad
1:

    movl $0x2BADB002, %eax         # MULTIBOOT_BOOTLOADER_MAGIC
    movl $MBI_ADDR, %ebx

    movl code32_start, %edx
    jmp *%edx

.align 8
gdt:
    .quad 0x0000000000000000       # null
    .quad 0x00CF9A000000FFFF       # 0x08: flat 32-bit code
    .quad 0x00CF92000000FFFF       # 0x10: flat 32-bit data
gdt_end:

gdt_desc:
    .word gdt_end - gdt - 1
    .long gdt

.set MBI_ADDR, 0x00080000
.set MBI_MOD_ADDR, 0x00080100
.set SETUP_STACK_TOP, 0x0009FC00
