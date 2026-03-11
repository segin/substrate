#define ASM_FILE 1

.section .zimage_header, "a"
.code16

/*
 * Minimal Linux boot-protocol setup stage.
 * Executed in 16-bit real mode by Linux loaders (e.g., LOADLIN/QEMU -kernel).
 * It switches to 32-bit protected mode, builds a tiny multiboot_info block
 * with cmdline pointer, then jumps to code32_start (kernel _start at 1MB).
 */

# Legacy entry point at the start of the setup image.
.global zimage_start
zimage_start:
    jmp setup_start

.set ZIMG_SCRATCH_SEG,      0x0000
.set ZIMG_SCRATCH_CMDLINE,  0x0500
.set ZIMG_SCRATCH_RAMDISK,  0x0504
.set ZIMG_SCRATCH_RDSIZE,   0x0508
.set ZIMG_SCRATCH_CODE32,   0x050C
.set ZIMG_SCRATCH_MMAPLEN,  0x0510

.org 0x1F1
setup_sects: .byte 0
root_flags:  .word 0
syssize:     .long 0
ram_size:    .word 0
vid_mode:    .word 0
root_dev:    .word 0
boot_flag:   .word 0xAA55

# Linux boot protocol header jump field at 0x200.
.org 0x200
    jmp setup_start

# Header start at 0x202
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

    /*
     * Linux boot protocol does not promise DS/ES already point at the setup
     * segment. Normalize them before touching local header/GDT symbols.
     */
    movw %cs, %ax
    subw $0x20, %ax                # setup enters at image_base + 0x200
    movw %ax, %ds
    xorw %ax, %ax
    movw %ax, %es

    /*
     * After switching to protected mode we run with flat base-0 segments, so
     * setup-image local symbols are no longer directly addressable. Snapshot
     * the Linux boot header values we need into fixed low-memory scratch.
     */
    movl cmd_line_ptr, %eax
    movl %eax, %es:ZIMG_SCRATCH_CMDLINE
    movl ramdisk_image, %eax
    movl %eax, %es:ZIMG_SCRATCH_RAMDISK
    movl ramdisk_size, %eax
    movl %eax, %es:ZIMG_SCRATCH_RDSIZE
    movl code32_start, %eax
    movl %eax, %es:ZIMG_SCRATCH_CODE32

    /*
     * Build a Multiboot-format mmap by querying BIOS E820 while we are still
     * in real mode. The kernel PMM relies on this to seed more than the tiny
     * bootstrap allocator window.
     */
    movw $MBI_MMAP_SEG, %ax
    movw %ax, %es
    xorw %si, %si
    xorl %ebx, %ebx
    xorl %ebp, %ebp

e820_loop:
    movw %si, %di
    addw $4, %di
    movl $0x00000014, %es:(%si)    # Multiboot entry payload size
    movl $0x0000E820, %eax
    movl $0x534D4150, %edx         # 'SMAP'
    movl $20, %ecx
    int $0x15
    jc e820_done
    cmpl $0x534D4150, %eax
    jne e820_done

    addw $24, %si
    addw $24, %bp
    testl %ebx, %ebx
    jnz e820_loop

e820_done:
    xorw %ax, %ax
    movw %ax, %es
    movzwl %bp, %eax
    movl %eax, %es:ZIMG_SCRATCH_MMAPLEN

    /*
     * Build an absolute far pointer to pm_entry. Once CR0.PE is set, a jump to
     * the raw symbol offset would land near address 0 instead of the actual
     * loaded setup image.
     */
    movw %ds, %ax
    movzwl %ax, %eax
    shll $4, %eax
    movl %eax, %edx
    addl $gdt, %edx
    movl %edx, gdt_desc + 2
    addl $pm_entry, %eax
    movl %eax, pm_jump_ptr
    movw $0x0008, pm_jump_ptr + 4

    # Enable A20 via fast A20 gate (port 0x92)
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    lgdt gdt_desc

    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0

    ljmpl *pm_jump_ptr

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

    movl $0x00000044, %eax         # MULTIBOOT_INFO_CMDLINE | MULTIBOOT_INFO_MEM_MAP
    movl $MBI_ADDR, %edi
    movl %eax, 0(%edi)

    movl ZIMG_SCRATCH_CMDLINE, %eax
    movl %eax, 16(%edi)            # multiboot_info_t.cmdline
    movl ZIMG_SCRATCH_MMAPLEN, %eax
    movl %eax, 44(%edi)            # multiboot_info_t.mmap_length
    movl $MBI_MMAP_ADDR, 48(%edi)  # multiboot_info_t.mmap_addr

    # If Linux loader provided initrd, expose it as one Multiboot module.
    movl ZIMG_SCRATCH_RDSIZE, %ecx
    testl %ecx, %ecx
    jz 1f
    movl ZIMG_SCRATCH_RAMDISK, %edx
    testl %edx, %edx
    jz 1f

    movl $0x0000004C, 0(%edi)      # MULTIBOOT_INFO_CMDLINE | MULTIBOOT_INFO_MODS | MULTIBOOT_INFO_MEM_MAP
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

    movl $0x00100000, %edx
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

pm_jump_ptr:
    .long 0
    .word 0

.set MBI_ADDR, 0x00080000
.set MBI_MOD_ADDR, 0x00080100
.set MBI_MMAP_ADDR, 0x00080200
.set MBI_MMAP_SEG, (MBI_MMAP_ADDR >> 4)
.set SETUP_STACK_TOP, 0x0009FC00
