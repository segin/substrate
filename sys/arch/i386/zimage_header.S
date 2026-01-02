#define ASM_FILE 1

.section .zimage_header, "a"
.code16

# Linux boot header at start of setup
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
type_of_loader: .byte 0
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
    # Minimal 16-bit to 32-bit transition would go here.
    # For now, this is just a header so loaders (like Syslinux)
    # can recognize the file and jump to code32_start.
    cli
    hlt
    jmp setup_start
