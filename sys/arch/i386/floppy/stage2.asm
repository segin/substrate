BITS 16
ORG 0

%ifndef KERNEL_LBA
%define KERNEL_LBA 0
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 0
%endif
%ifndef KERNEL_SETUP_SECTORS
%define KERNEL_SETUP_SECTORS 0
%endif

%define FLOPPY_SPT 18
%define FLOPPY_HEADS 2

%define STAGE2_SEG 0x0800
%define STACK_SEG  0x7000
%define STACK_TOP  0xFFFE
%define LOWBUF_SEG 0x1000
%define SETUP_SEG  0x9000
%define SETUP_ENTRY_SEG (SETUP_SEG + 0x20)
%define CMDLINE_SEG 0x8800
%define CMDLINE_PHYS 0x00088000
%define KERNEL_LOAD_ADDR 0x00100000
%define CMDLINE_MAX 256

%define SETUP_HDR_TYPE_OF_LOADER 0x210
%define SETUP_HDR_CMDLINE_PTR    0x228

start:
    cli
    mov al, 'A'
    out 0xE9, al
    mov ax, STAGE2_SEG
    mov ds, ax
    mov ax, STACK_SEG
    mov ss, ax
    mov sp, STACK_TOP
    sti

    mov [boot_drive], dl

    mov si, banner
    call puts
    call check_386
    jnc .cpu_ok
    mov si, cpu_msg
    call puts
    jmp halt
.cpu_ok:
    call enable_a20
    call prompt_cmdline
    call prepare_cmdline
    call load_setup_image
    jc disk_error
    call patch_setup_header
    call load_kernel_body
    jc disk_error

    mov si, boot_msg
    call puts
    mov al, 'J'
    out 0xE9, al
    mov dl, [boot_drive]
    jmp SETUP_ENTRY_SEG:0x0000

disk_error:
    mov si, disk_msg
    call puts
    jmp halt

check_386:
    pushf
    pop ax
    mov cx, ax
    xor ax, 0x7000
    push ax
    popf
    pushf
    pop ax
    push cx
    popf
    xor ax, cx
    and ax, 0x7000
    jz .fail
    clc
    ret
.fail:
    stc
    ret

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

prompt_cmdline:
    mov byte [cmdline_len], 0
    mov si, prompt
    call puts
.read_key:
    xor ah, ah
    int 0x16
    cmp al, 0x0D
    je .done
    cmp al, 0x08
    je .backspace
    cmp al, 0x20
    jb .read_key
    cmp al, 0x7E
    ja .read_key
    mov bl, [cmdline_len]
    cmp bl, CMDLINE_MAX - 1
    jae .read_key
    mov [cmdline_buf + bx], al
    inc byte [cmdline_len]
    call putc
    jmp .read_key
.backspace:
    cmp byte [cmdline_len], 0
    je .read_key
    dec byte [cmdline_len]
    mov al, 0x08
    call putc
    mov al, ' '
    call putc
    mov al, 0x08
    call putc
    jmp .read_key
.done:
    mov al, 'P'
    out 0xE9, al
    mov al, 13
    call putc
    mov al, 10
    call putc
    mov bl, [cmdline_len]
    mov byte [cmdline_buf + bx], 0
    ret

prepare_cmdline:
    push ds
    push es
    mov ax, cs
    mov ds, ax
    mov ax, CMDLINE_SEG
    mov es, ax
    xor di, di
    cmp byte [cmdline_len], 0
    jne .typed
    mov si, default_cmdline
    jmp .copy
.typed:
    mov si, cmdline_buf
.copy:
    lodsb
    stosb
    test al, al
    jnz .copy
    pop es
    pop ds
    mov dword [cmdline_ptr_value], CMDLINE_PHYS
    ret

patch_setup_header:
    mov al, 'S'
    out 0xE9, al
    push es
    mov ax, SETUP_SEG
    mov es, ax
    mov byte [es:SETUP_HDR_TYPE_OF_LOADER], 0xFD
    mov eax, [cmdline_ptr_value]
    mov [es:SETUP_HDR_CMDLINE_PTR], eax
    pop es
    ret

load_setup_image:
    push es
    mov ax, SETUP_SEG
    mov es, ax
    xor bx, bx
    mov si, KERNEL_LBA
    mov cx, KERNEL_SETUP_SECTORS
.loop:
    cmp cx, 0
    je .done
    mov byte [io_sector_count], 1
    mov ax, si
    call read_sectors
    jc .fail
    add bx, 512
    inc si
    dec cx
    jmp .loop
.done:
    pop es
    clc
    ret
.fail:
    pop es
    stc
    ret

load_kernel_body:
    mov al, 'L'
    out 0xE9, al
    mov word [kernel_lba], KERNEL_LBA + KERNEL_SETUP_SECTORS
    mov word [kernel_remaining], KERNEL_SECTORS - KERNEL_SETUP_SECTORS
    mov dword [kernel_dest], KERNEL_LOAD_ADDR
.next_sector:
    cmp word [kernel_remaining], 0
    je .done

    mov ax, [kernel_lba]
    xor dx, dx
    mov bx, FLOPPY_SPT
    div bx
    mov al, FLOPPY_SPT
    sub al, dl
    xor ah, ah
    cmp ax, [kernel_remaining]
    jbe .chunk_ready
    mov ax, [kernel_remaining]
.chunk_ready:
    mov [io_sector_count], al
    mov al, 'k'
    out 0xE9, al

    push ds
    push es
    mov ax, LOWBUF_SEG
    mov es, ax
    xor bx, bx
    mov ax, [kernel_lba]
    call read_sectors
    jc .fail_pop
    pop es
    pop ds
    mov al, 'r'
    out 0xE9, al
    call enter_unreal
    mov al, 'u'
    out 0xE9, al

    mov edi, [kernel_dest]
    xor cx, cx
    mov cl, [io_sector_count]
    shl cx, 7
    mov ax, LOWBUF_SEG
    mov ds, ax
    xor si, si
.copy_dwords:
    mov eax, [si]
    mov [fs:edi], eax
    add si, 4
    add edi, 4
    loop .copy_dwords

    mov ax, cs
    mov ds, ax
    mov [kernel_dest], edi
    xor ax, ax
    mov al, [io_sector_count]
    add word [kernel_lba], ax
    sub word [kernel_remaining], ax
    mov al, 'c'
    out 0xE9, al
    jmp .next_sector

.fail_pop:
    pop es
    pop ds
    stc
    ret
.done:
    mov ax, STAGE2_SEG
    mov ds, ax
    clc
    ret

read_sectors:
    push ax
    push bx
    push cx
    push dx

    call lba_to_chs
    mov dl, [cs:boot_drive]
    mov ah, 0x02
    mov al, [cs:io_sector_count]
    int 0x13
    jnc .ok

    xor ax, ax
    int 0x13
    stc
    jmp .out
.ok:
    clc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

lba_to_chs:
    push bx
    xor dx, dx
    mov bx, FLOPPY_SPT * FLOPPY_HEADS
    div bx
    mov ch, al

    mov ax, dx
    xor dx, dx
    mov bl, FLOPPY_SPT
    div bl
    mov dh, al
    mov cl, ah
    inc cl
    pop bx
    ret

putc:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    ret

puts:
    lodsb
    test al, al
    jz .done
    call putc
    jmp puts
.done:
    ret

halt:
    cli
.hlt:
    hlt
    jmp .hlt

enter_unreal:
    cli
    mov al, 'g'
    out 0xE9, al
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, gdt
    mov [gdt_desc + 2], eax
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp 0x08:pm_unreal

pm_unreal:
    mov al, 'p'
    out 0xE9, al
    mov ax, 0x10
    mov fs, ax
    mov gs, ax
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    mov al, 'q'
    out 0xE9, al
    db 0xEA
    dw rm_unreal
    dw STAGE2_SEG

BITS 16
rm_unreal:
    mov al, 'm'
    out 0xE9, al
    mov ax, STAGE2_SEG
    mov ds, ax
    sti
    ret

align 8
gdt:
    dq 0x0000000000000000
    dq 0x00009A008000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

boot_drive: db 0
io_sector_count: db 0
cmdline_len: db 0
cmdline_ptr_value: dd 0
kernel_dest: dd 0
kernel_lba: dw 0
kernel_remaining: dw 0

banner: db 13, 10, 'Substrate floppy loader', 13, 10, 0
prompt: db 'boot [Enter=default]: ', 0
cpu_msg: db 'Substrate requires an i386 or newer CPU.', 13, 10, 0
disk_msg: db 'Kernel load failed.', 13, 10, 0
boot_msg: db 'Booting kernel...', 13, 10, 0
default_cmdline: db 'serial_debug root=/dev/storage/ide0 rootfstype=ext2 init=/bin/sh', 0

cmdline_buf: times CMDLINE_MAX db 0
