BITS 16
ORG 0x7C00

%ifndef STAGE2_SEG
%define STAGE2_SEG 0x0800
%endif
%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 0
%endif
%define FLOPPY_SPT 18
%define FLOPPY_HEADS 2

start:
    cli
    mov al, '1'
    out 0xE9, al
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, banner
    call puts

    mov ax, STAGE2_SEG
    mov es, ax
    xor bx, bx
    mov si, 1
    mov cx, STAGE2_SECTORS
.load_stage2:
    cmp cx, 0
    je .done
    mov ax, si
    call read_sector
    jc disk_error
    add bx, 512
    inc si
    dec cx
    jmp .load_stage2
.done:
    mov al, '2'
    out 0xE9, al
    mov dl, [boot_drive]
    jmp STAGE2_SEG:0

disk_error:
    mov si, disk_msg
    call puts
    jmp hang

read_sector:
    push ax
    push bx
    push cx
    push dx

    call lba_to_chs
    mov dl, [boot_drive]
    mov ah, 0x02
    mov al, 0x01
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
    push ax
    push bx
    push dx

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

    pop dx
    pop bx
    pop ax
    ret

puts:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp puts
.done:
    ret

hang:
    cli
.hlt:
    hlt
    jmp .hlt

boot_drive: db 0
banner: db 'Substrate floppy boot', 13, 10, 0
disk_msg: db 'Stage2 load failed', 13, 10, 0

times 510-($-$$) db 0
    dw 0xAA55
