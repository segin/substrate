;=============================================================================
; sys/boot/stage1.asm - Substrate ext2 boot block (stage1 + stage1.5)
;
; Fits in the ext2 boot block (bytes 0-1023 of the filesystem).
; Stage1 (sector 0): BIOS loads to 0x7C00, reads sector 1 + superblock.
; Stage1.5 (sector 1): Parses ext2 metadata to find inode 5 (boot loader
;   reserved inode), loads stage2 to STAGE2_ADDR, obtains BIOS E820
;   memory map, enters protected mode, and jumps to stage2_main().
;=============================================================================

[BITS 16]
[ORG 0x7C00]

STAGE2_SEG       equ 0x0500         ; segment => physical 0x5000
STAGE2_OFF       equ 0x0000         ; real-mode boot prompt entry
STAGE2_PHYS      equ 0x5000         ; flat physical address
STAGE2_ENTRY_PHYS equ 0x5800        ; protected-mode stage2 entry after boot16 blob
INODE_BUF        equ 0x1000         ; where we read the inode table block
BGD_BUF          equ 0x8000         ; where we read the block group descriptor
BOOTLINE_BUF     equ 0x0800         ; BIOS prompt buffer shared with stage2
EXT2_MAGIC       equ 0xEF53
BLOCK_SIZE       equ 1024
SECTORS_PER_BLK  equ 2

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    and dl, 0x80
    jz halt
    mov [drive], dl

    mov word [dap_count], 6
    mov word [dap_dest], 0x7E00
    mov dword [dap_lba], 1
    call read_disk

    jmp stage1_5

read_disk:
    push si
    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    pop si
    jc .disk_err
    ret
.disk_err:
    mov si, msg_disk_err
    call print
    jmp halt

print:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

halt:
    cli
    hlt
    jmp halt

msg_disk_err:   db "Disk error", 13, 10, 0
drive:          db 0

align 4
dap:
    db 0x10
    db 0
dap_count:
    dw 0
dap_dest:
    dw 0
    dw 0
dap_lba:
    dd 0
    dd 0

times 510 - ($ - $$) db 0
dw 0xAA55

stage1_5:
    mov ax, [0x8000 + 56]
    cmp ax, EXT2_MAGIC
    jne .not_ext2

    mov eax, [0x8400 + 8]
    shl eax, 1
    mov [dap_lba], eax
    mov word [dap_count], SECTORS_PER_BLK
    mov word [dap_dest], INODE_BUF
    call read_disk

    mov bx, INODE_BUF + 512
    mov ecx, [bx + 4]
    test ecx, ecx
    jz .no_stage2

    mov edx, [bx + 28]
    shr edx, 1
    test edx, edx
    jz .no_stage2

    lea si, [bx + 40]
    push STAGE2_SEG
    pop es
    xor di, di

    mov cx, dx
    cmp cx, 12
    ja .too_big

.read_blocks:
    mov eax, [si]
    test eax, eax
    jz .done_reading
    shl eax, 1
    mov [dap_lba], eax
    mov word [dap_count], SECTORS_PER_BLK
    mov [dap_dest], di
    mov [dap_dest + 2], es
    call read_disk
    add di, BLOCK_SIZE
    test di, di
    jnz .no_seg_wrap
    mov ax, es
    add ax, 0x1000
    mov es, ax
.no_seg_wrap:
    add si, 4
    dec cx
    jnz .read_blocks

.done_reading:
    xor ax, ax
    mov es, ax

    ; BIOS-backed boot prompt lives at the start of the stage2 image.
    call STAGE2_SEG:STAGE2_OFF

    call get_e820_mmap
    jmp enter_pm

.not_ext2:
    mov si, msg_no_ext2
    call print
    jmp halt

.no_stage2:
    mov si, msg_no_stage2
    call print
    jmp halt

.too_big:
    mov si, msg_too_big
    call print
    jmp halt

get_e820_mmap:
    mov di, e820_map
    xor ebx, ebx
    mov [e820_count], dword 0

.e820_loop:
    mov edx, 0x534D4150
    mov eax, 0xE820
    mov ecx, 24
    mov [es:di + 20], dword 1
    int 0x15
    jc .e820_done

    cmp eax, 0x534D4150
    jne .e820_done

    mov eax, [es:di + 8]
    or eax, [es:di + 12]
    jz .e820_skip

    add di, 24
    inc dword [e820_count]
    cmp dword [e820_count], 32
    jge .e820_done

.e820_skip:
    test ebx, ebx
    jnz .e820_loop

.e820_done:
    ret

enter_pm:
    cli
    in al, 0x92
    or al, 2
    out 0x92, al
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

[BITS 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00090000

    push dword BOOTLINE_BUF
    xor eax, eax
    mov al, [drive]
    push eax
    push dword e820_map
    push dword [e820_count]

    mov eax, STAGE2_ENTRY_PHYS
    call eax

    cli
    hlt

[BITS 16]
align 8
gdt:
    dq 0
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

msg_no_ext2:    db "Not ext2", 13, 10, 0
msg_no_stage2:  db "No inode 5", 13, 10, 0
msg_too_big:    db "stage2 >12K", 13, 10, 0

e820_count:     dd 0

times 1024 - ($ - $$) db 0

e820_map equ 0x0500
