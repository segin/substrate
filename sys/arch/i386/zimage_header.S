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
.set ZIMG_SCRATCH_MEMLOW,   0x0514
.set ZIMG_SCRATCH_MEMUPPER, 0x0518
.set ZIMG_SCRATCH_VBE_132X25, 0x051C
.set ZIMG_SCRATCH_VBE_132X43, 0x051E
.set ZIMG_SCRATCH_VBE_132X50, 0x0520
.set ZIMG_SCRATCH_VBE_132X60, 0x0522
.set ZIMG_SCRATCH_CMDLINE_BUF, 0x0600
.set ZIMG_SCRATCH_CMDLINE_MAX, 256
.set ZIMG_SCRATCH_VBE_INFO, 0x0800
.set ZIMG_SCRATCH_VBE_MODE, 0x0A00

.set VBE_INFO_MODEPTR_OFF, 0x0E
.set VBE_INFO_MODEPTR_SEG, 0x10
.set VBE_MODE_ATTR, 0x00
.set VBE_MODE_XRES, 0x12
.set VBE_MODE_YRES, 0x14
.set VBE_MODE_XCHAR, 0x16
.set VBE_MODE_YCHAR, 0x17

.set MBI_FLAG_MEMORY,  0x00000001
.set MBI_FLAG_CMDLINE, 0x00000004
.set MBI_FLAG_MODS,    0x00000008
.set MBI_FLAG_MEM_MAP, 0x00000040

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
    call copy_cmdline_to_scratch
    movl $ZIMG_SCRATCH_CMDLINE_BUF, %eax
    movl %eax, %es:ZIMG_SCRATCH_CMDLINE
    movl ramdisk_image, %eax
    movl %eax, %es:ZIMG_SCRATCH_RAMDISK
    movl ramdisk_size, %eax
    movl %eax, %es:ZIMG_SCRATCH_RDSIZE
    movl code32_start, %eax
    movl %eax, %es:ZIMG_SCRATCH_CODE32

    /*
     * Record basic memory sizing for old BIOSes that do not implement E820.
     * mem_lower is always taken from INT 12h (conventional RAM in KB).
     * mem_upper falls back to E801, then AH=88h if no E820 map is available.
     */
    int $0x12
    movzwl %ax, %eax
    movl %eax, %es:ZIMG_SCRATCH_MEMLOW
    xorl %eax, %eax
    movl %eax, %es:ZIMG_SCRATCH_MEMUPPER
    movw %ax, %es:ZIMG_SCRATCH_VBE_132X25
    movw %ax, %es:ZIMG_SCRATCH_VBE_132X43
    movw %ax, %es:ZIMG_SCRATCH_VBE_132X50
    movw %ax, %es:ZIMG_SCRATCH_VBE_132X60

    call maybe_vga_ask

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

    testw %bp, %bp
    jnz memory_probe_done

    movw $0xE801, %ax
    int $0x15
    jc memory_probe_88h

    movzwl %ax, %eax
    movzwl %bx, %ebx
    testl %eax, %eax
    jne memory_probe_e801_done
    testl %ebx, %ebx
    jne memory_probe_e801_done
    movzwl %cx, %eax
    movzwl %dx, %ebx

memory_probe_e801_done:
    shll $6, %ebx
    addl %ebx, %eax
    movl %eax, %es:ZIMG_SCRATCH_MEMUPPER
    jmp memory_probe_done

memory_probe_88h:
    xorw %ax, %ax
    movb $0x88, %ah
    int $0x15
    jc memory_probe_done
    movzwl %ax, %eax
    movl %eax, %es:ZIMG_SCRATCH_MEMUPPER

memory_probe_done:

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

copy_cmdline_to_scratch:
    pushw %ds
    pushw %es
    pushw %si
    pushw %di
    pushw %cx
    pushw %dx

    xorw %dx, %dx
    movw %dx, %es
    movw $ZIMG_SCRATCH_CMDLINE_BUF, %di
    movw $ZIMG_SCRATCH_CMDLINE_MAX - 1, %cx
    testl %eax, %eax
    jnz 1f
    movb $0, %es:(%di)
    jmp 3f
1:
    movl %eax, %edx
    andw $0x000F, %dx
    shrl $4, %eax
    movw %ax, %ds
    movw %dx, %si
2:
    lodsb
    stosb
    testb %al, %al
    jz 3f
    loop 2b
    movb $0, %es:(%di)
3:
    popw %dx
    popw %cx
    popw %di
    popw %si
    popw %es
    popw %ds
    ret

maybe_vga_ask:
    pushw %ds
    pushw %es
    pushw %si
    pushw %ax

    xorw %ax, %ax
    movw %ax, %ds
    movw $ZIMG_SCRATCH_CMDLINE_BUF, %si
1:
    lodsb
    testb %al, %al
    jz 5f
    cmpb $'v', %al
    jne 1b
    cmpw $ZIMG_SCRATCH_CMDLINE_BUF + 1, %si
    je 2f
    cmpb $' ', -2(%si)
    jne 1b
2:
    cmpb $'g', (%si)
    jne 1b
    cmpb $'a', 1(%si)
    jne 1b
    cmpb $'=', 2(%si)
    jne 1b
    cmpb $'a', 3(%si)
    jne 1b
    cmpb $'s', 4(%si)
    jne 1b
    cmpb $'k', 5(%si)
    jne 1b
    movb 6(%si), %al
    testb %al, %al
    jz 3f
    cmpb $' ', %al
    jne 1b
3:
    call query_vbe_text_modes
    call vga_ask_menu
    jmp 5f
5:
    popw %ax
    popw %si
    popw %es
    popw %ds
    ret

putc_bios:
    pushw %ax
    pushw %bx
    cmpb $10, %al
    jne 1f
    movb $13, %al
    call putc_bios
    movb $10, %al
1:
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    popw %bx
    popw %ax
    ret

puts_bios:
    pushw %ax
1:
    lodsb
    testb %al, %al
    jz 2f
    call putc_bios
    jmp 1b
2:
    popw %ax
    ret

getc_bios:
    xorw %ax, %ax
    int $0x16
    ret

append_textmode_token:
    pushw %es
    pushw %di
    pushw %cx
    pushw %ax

    xorw %ax, %ax
    movw %ax, %es
    movw $ZIMG_SCRATCH_CMDLINE_BUF, %di
    movw $ZIMG_SCRATCH_CMDLINE_MAX - 1, %cx
1:
    movb %es:(%di), %al
    testb %al, %al
    jz 2f
    incw %di
    loop 1b
    jmp 4f
2:
    cmpw $ZIMG_SCRATCH_CMDLINE_BUF, %di
    je 3f
    movb $' ', %es:(%di)
    incw %di
3:
    lodsb
    testb %al, %al
    jz 4f
    movb %al, %es:(%di)
    incw %di
    jmp 3b
4:
    movb $0, %es:(%di)
    popw %ax
    popw %cx
    popw %di
    popw %es
    ret

append_bios_textmode_flag:
    movw $textmode_bios_flag, %si
    call append_textmode_token
    ret

query_vbe_text_modes:
    pushw %ds
    pushw %es
    pushw %si
    pushw %di
    pushw %bx
    pushw %cx
    pushw %dx
    pushw %ax

    xorw %ax, %ax
    movw %ax, %es
    movw $ZIMG_SCRATCH_VBE_INFO, %di
    movl $0x32454256, %es:(%di)
    movw $0x4F00, %ax
    int $0x10
    cmpw $0x004F, %ax
    jne 9f

    movw %es:ZIMG_SCRATCH_VBE_INFO + VBE_INFO_MODEPTR_SEG, %dx
    movw %es:ZIMG_SCRATCH_VBE_INFO + VBE_INFO_MODEPTR_OFF, %bx
    testw %dx, %dx
    jz 9f

    movw %ds, %ax
    pushw %ax
    movw %dx, %ds
    movw %bx, %si
1:
    lodsw
    cmpw $0xFFFF, %ax
    je 8f
    pushw %si
    movw %ax, %cx
    xorw %ax, %ax
    movw %ax, %es
    movw $ZIMG_SCRATCH_VBE_MODE, %di
    movw $0x4F01, %ax
    int $0x10
    cmpw $0x004F, %ax
    jne 7f

    movw %es:ZIMG_SCRATCH_VBE_MODE + VBE_MODE_ATTR, %ax
    testw $0x0001, %ax
    jz 7f
    testw $0x0010, %ax
    jnz 7f
    movb %es:ZIMG_SCRATCH_VBE_MODE + VBE_MODE_XCHAR, %al
    testb %al, %al
    jz 7f
    movb %es:ZIMG_SCRATCH_VBE_MODE + VBE_MODE_YCHAR, %al
    testb %al, %al
    jz 7f
    movw %es:ZIMG_SCRATCH_VBE_MODE + VBE_MODE_XRES, %ax
    cmpw $132, %ax
    jne 7f
    movw %es:ZIMG_SCRATCH_VBE_MODE + VBE_MODE_YRES, %ax
    cmpw $25, %ax
    je 2f
    cmpw $43, %ax
    je 3f
    cmpw $50, %ax
    je 4f
    cmpw $60, %ax
    je 5f
    jmp 7f
2:
    movw %cx, %es:ZIMG_SCRATCH_VBE_132X25
    jmp 7f
3:
    movw %cx, %es:ZIMG_SCRATCH_VBE_132X43
    jmp 7f
4:
    movw %cx, %es:ZIMG_SCRATCH_VBE_132X50
    jmp 7f
5:
    movw %cx, %es:ZIMG_SCRATCH_VBE_132X60
7:
    popw %si
    jmp 1b
8:
    popw %ds
9:
    popw %ax
    popw %dx
    popw %cx
    popw %bx
    popw %di
    popw %si
    popw %es
    popw %ds
    ret

vga_select_vbe_textmode:
    pushw %si
    movw $0x4F02, %ax
    int $0x10
    movw %dx, %si
    call append_textmode_token
    call append_bios_textmode_flag
    popw %si
    ret

vga_select_80x25:
    movw $0x0003, %ax
    int $0x10
    movw $textmode_80x25, %si
    call append_textmode_token
    call append_bios_textmode_flag
    ret

vga_select_80x43:
    movw $0x0003, %ax
    int $0x10
    movw $0x1111, %ax
    xorw %bx, %bx
    int $0x10
    movw $textmode_80x43, %si
    call append_textmode_token
    call append_bios_textmode_flag
    ret

vga_select_80x50:
    movw $0x0003, %ax
    int $0x10
    movw $0x1112, %ax
    xorw %bx, %bx
    int $0x10
    movw $textmode_80x50, %si
    call append_textmode_token
    call append_bios_textmode_flag
    ret

vga_ask_menu:
    pushw %ds
    pushw %es
    pushw %si
    movw %cs, %ax
    subw $0x20, %ax
    movw %ax, %ds
    movw $vga_menu, %si
    call puts_bios
    xorw %ax, %ax
    movw %ax, %es
    cmpw $0, %es:ZIMG_SCRATCH_VBE_132X25
    je 1f
    movw $vga_menu_132x25, %si
    call puts_bios
1:
    cmpw $0, %es:ZIMG_SCRATCH_VBE_132X43
    je 2f
    movw $vga_menu_132x43, %si
    call puts_bios
2:
    cmpw $0, %es:ZIMG_SCRATCH_VBE_132X50
    je 3f
    movw $vga_menu_132x50, %si
    call puts_bios
3:
    cmpw $0, %es:ZIMG_SCRATCH_VBE_132X60
    je 4f
    movw $vga_menu_132x60, %si
    call puts_bios
4:
    movw $vga_menu_prompt, %si
    call puts_bios
5:
    call getc_bios
    cmpb $'1', %al
    je 6f
    cmpb $'2', %al
    je 7f
    cmpb $'3', %al
    je 8f
    cmpb $'4', %al
    je 9f
    cmpb $'5', %al
    je 10f
    cmpb $'6', %al
    je 11f
    cmpb $'7', %al
    je 12f
    jmp 5b
6:
    call putc_bios
    movb $10, %al
    call putc_bios
    call vga_select_80x25
    jmp 13f
7:
    call putc_bios
    movb $10, %al
    call putc_bios
    call vga_select_80x43
    jmp 13f
8:
    call putc_bios
    movb $10, %al
    call putc_bios
    call vga_select_80x50
    jmp 13f
9:
    movw %es:ZIMG_SCRATCH_VBE_132X25, %bx
    testw %bx, %bx
    jz 5b
    call putc_bios
    movb $10, %al
    call putc_bios
    movw $textmode_132x25, %dx
    call vga_select_vbe_textmode
    jmp 13f
10:
    movw %es:ZIMG_SCRATCH_VBE_132X43, %bx
    testw %bx, %bx
    jz 5b
    call putc_bios
    movb $10, %al
    call putc_bios
    movw $textmode_132x43, %dx
    call vga_select_vbe_textmode
    jmp 13f
11:
    movw %es:ZIMG_SCRATCH_VBE_132X50, %bx
    testw %bx, %bx
    jz 5b
    call putc_bios
    movb $10, %al
    call putc_bios
    movw $textmode_132x50, %dx
    call vga_select_vbe_textmode
    jmp 13f
12:
    movw %es:ZIMG_SCRATCH_VBE_132X60, %bx
    testw %bx, %bx
    jz 5b
    call putc_bios
    movb $10, %al
    call putc_bios
    movw $textmode_132x60, %dx
    call vga_select_vbe_textmode
13:
    popw %si
    popw %es
    popw %ds
    ret

vga_menu:
    .asciz "\r\nSubstrate VGA text mode selector\r\n1. 80x25\r\n2. 80x43\r\n3. 80x50\r\n"
vga_menu_132x25:
    .asciz "4. 132x25 (VBE)\r\n"
vga_menu_132x43:
    .asciz "5. 132x43 (VBE)\r\n"
vga_menu_132x50:
    .asciz "6. 132x50 (VBE)\r\n"
vga_menu_132x60:
    .asciz "7. 132x60 (VBE)\r\n"
vga_menu_prompt:
    .asciz "Select mode [1-7]: "

textmode_80x25:
    .asciz "textmode=80x25"
textmode_80x43:
    .asciz "textmode=80x43"
textmode_80x50:
    .asciz "textmode=80x50"
textmode_132x25:
    .asciz "textmode=132x25"
textmode_132x43:
    .asciz "textmode=132x43"
textmode_132x50:
    .asciz "textmode=132x50"
textmode_132x60:
    .asciz "textmode=132x60"
textmode_bios_flag:
    .asciz "textmode_bios=1"

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

    movl $(MBI_FLAG_CMDLINE | MBI_FLAG_MEMORY), %eax
    movl $MBI_ADDR, %edi
    movl %eax, 0(%edi)
    movl ZIMG_SCRATCH_MEMLOW, %eax
    movl %eax, 4(%edi)             # multiboot_info_t.mem_lower
    movl ZIMG_SCRATCH_MEMUPPER, %eax
    movl %eax, 8(%edi)             # multiboot_info_t.mem_upper

    movl ZIMG_SCRATCH_CMDLINE, %eax
    movl %eax, 16(%edi)            # multiboot_info_t.cmdline
    movl ZIMG_SCRATCH_MMAPLEN, %eax
    testl %eax, %eax
    jz 1f
    orl $MBI_FLAG_MEM_MAP, 0(%edi)
    movl %eax, 44(%edi)            # multiboot_info_t.mmap_length
    movl $MBI_MMAP_ADDR, 48(%edi)  # multiboot_info_t.mmap_addr
1:

    # If Linux loader provided initrd, expose it as one Multiboot module.
    movl ZIMG_SCRATCH_RDSIZE, %ecx
    testl %ecx, %ecx
    jz 2f
    movl ZIMG_SCRATCH_RAMDISK, %edx
    testl %edx, %edx
    jz 2f

    orl $MBI_FLAG_MODS, 0(%edi)
    movl $1, 20(%edi)              # multiboot_info_t.mods_count
    movl $MBI_MOD_ADDR, 24(%edi)   # multiboot_info_t.mods_addr
    movl %edx, MBI_MOD_ADDR        # module[0].mod_start
    addl %ecx, %edx
    movl %edx, MBI_MOD_ADDR + 4    # module[0].mod_end
    movl $0, MBI_MOD_ADDR + 8      # module[0].cmdline
    movl $0, MBI_MOD_ADDR + 12     # module[0].pad
2:

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
