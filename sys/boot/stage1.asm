;=============================================================================
; sys/boot/stage1.asm - Substrate ext2 boot block (stage1 + stage1.5)
;
; Fits in the ext2 boot block (bytes 0-1023 of the filesystem).
; Stage1 (sector 0): BIOS loads to 0x7C00, reads sector 1 + superblock.
; Stage1.5 (sector 1): Parses ext2 metadata to find inode 5 (boot loader
;   reserved inode), loads stage2 to STAGE2_ADDR, obtains BIOS E820
;   memory map, enters protected mode, and jumps to stage2_main().
;
; Requirements:
;   - ext2 filesystem with 1024-byte blocks and 128-byte inodes
;   - stage2 binary written to inode 5 (max 12 direct blocks = 12 KB)
;   - BIOS supporting INT 13h extended read (LBA)
;
; Memory layout during boot:
;   0x0500 - 0x0600  E820 memory map scratch
;   0x1000 - 0x1400  inode table block (1024 bytes)
;   0x5000 - 0x7BFF  stage2 load area (STAGE2_SEG:0)
;   0x7C00 - 0x7DFF  stage1 (this code, sector 0)
;   0x7E00 - 0x7FFF  stage1.5 + superblock (sector 1)
;   0x8000 - 0x83FF  block group descriptor block
;   0x9000:0         stack (grows down from 0x90000)
;=============================================================================

[BITS 16]
[ORG 0x7C00]

STAGE2_SEG      equ 0x0500          ; segment => physical 0x5000
STAGE2_OFF      equ 0x0000
STAGE2_PHYS     equ 0x5000          ; flat physical address
INODE_BUF       equ 0x1000          ; where we read the inode table block
BGD_BUF         equ 0x8000          ; where we read the block group descriptor
E820_BUF        equ 0x0500          ; E820 scratch (before stage2 overwrites it;
                                    ; we copy into stage2 area later)
; ext2 constants
EXT2_MAGIC      equ 0xEF53
INODE_SIZE      equ 128
BLOCK_SIZE      equ 1024
SECTORS_PER_BLK equ 2               ; 1024 / 512

;=============================================================================
; STAGE 1 - Sector 0 (loaded by BIOS)
;=============================================================================
entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00                  ; stack just below us, grows down
    sti

    ; Save BIOS drive number
    and dl, 0x80
    jz .no_drive
    mov [drive], dl

    ; Print banner
    mov si, msg_boot
    call print

    ; Read 6 sectors starting at LBA 1 into 0x7E00
    ; This gets: sector 1 (our stage1.5), sectors 2-3 (superblock),
    ;            sectors 4-5 (block group descriptors)
    ; Actually: superblock is at byte 1024 = LBA 2.  We load LBA 1..6
    ; LBA 1 = our second 512 bytes (stage1.5 code)
    ; LBA 2-3 = superblock (1024 bytes)
    ; LBA 4-5 = block group descriptor table (1024 bytes)
    mov word [dap_count], 6
    mov word [dap_dest], 0x7E00
    mov dword [dap_lba], 1
    call read_disk

    jmp stage1_5

.no_drive:
    mov si, msg_no_drive
    call print
    jmp halt

;-----------------------------------------------------------------------------
; read_disk - INT 13h extended read using DAP
;-----------------------------------------------------------------------------
read_disk:
    push si                         ; preserve caller's SI
    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    pop si                          ; restore SI (does not affect CF)
    jc .disk_err
    ret
.disk_err:
    mov si, msg_disk_err
    call print
    jmp halt

;-----------------------------------------------------------------------------
; print - Print ASCIIZ string at DS:SI via BIOS
;-----------------------------------------------------------------------------
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

;-----------------------------------------------------------------------------
; Data for stage1
;-----------------------------------------------------------------------------
msg_boot:       db "Substrate boot", 13, 10, 0
msg_no_drive:   db "No drive", 13, 10, 0
msg_disk_err:   db "Disk error", 13, 10, 0
drive:          db 0

; DAP (Disk Address Packet) for INT 13h extended read
align 4
dap:
    db 0x10                         ; packet size
    db 0                            ; reserved
dap_count:
    dw 0                            ; sector count
dap_dest:
    dw 0                            ; destination offset
    dw 0                            ; destination segment
dap_lba:
    dd 0                            ; LBA (low 32)
    dd 0                            ; LBA (high 32)

; Pad to 510 bytes + boot signature
times 510 - ($ - $$) db 0
dw 0xAA55

;=============================================================================
; STAGE 1.5 - Sector 1
; At this point, memory layout:
;   0x7E00 = this code (stage1.5)
;   0x8000 = ext2 superblock   (LBA 2-3)
;   0x8400 = block group desc  (LBA 4-5)
;=============================================================================
stage1_5:
    ; Superblock is at 0x8000 (we loaded LBA 2-3 there)
    ; Actually: stage1 loaded LBA 1-6 starting at 0x7E00
    ; LBA 1 => 0x7E00 (this code, 512 bytes)
    ; LBA 2 => 0x8000 (superblock first half)
    ; LBA 3 => 0x8200 (superblock second half)
    ; LBA 4 => 0x8400 (BGD first half)
    ; LBA 5 => 0x8600 (BGD second half)
    ; LBA 6 => 0x8800

    ; Check ext2 magic at superblock offset 56 (0x38)
    mov ax, [0x8000 + 56]           ; s_magic
    cmp ax, EXT2_MAGIC
    jne .not_ext2

    mov si, msg_ext2_ok
    call print

    ; Read inode table block for inode 5
    ; Block group descriptor[0].bg_inode_table is at BGD+8
    mov eax, [0x8400 + 8]           ; bg_inode_table (block number)

    ; Inode 5 is index 4 in the table.  Each inode is 128 bytes.
    ; Offset within inode table = 4 * 128 = 512 bytes = 1 sector
    ; Block containing inode 5 = bg_inode_table + (512 / 1024) = bg_inode_table
    ; Offset within that block = 512

    ; Read the inode table block (1024 bytes = 2 sectors)
    ; LBA = block_number * SECTORS_PER_BLK
    shl eax, 1                      ; eax = block * 2 = LBA
    mov [dap_lba], eax
    mov word [dap_count], SECTORS_PER_BLK
    mov word [dap_dest], INODE_BUF
    call read_disk

    ; Inode 5 is at offset 4*128 = 512 within the block
    mov bx, INODE_BUF + 512

    ; Check i_size (offset 4 in inode)
    mov ecx, [bx + 4]               ; i_size
    test ecx, ecx
    jz .no_stage2

    ; i_blocks at offset 28 (in 512-byte sectors)
    mov edx, [bx + 28]
    shr edx, 1                      ; convert 512-byte sectors to 1024-byte blocks
    test edx, edx
    jz .no_stage2

    ; Load stage2 from direct block pointers (offset 40 in inode)
    ; We support up to 12 direct blocks (12 KB)
    lea si, [bx + 40]               ; si -> i_block[0]
    push STAGE2_SEG
    pop es
    xor di, di                      ; es:di = STAGE2_SEG:0 = physical STAGE2_PHYS

    mov cx, dx                      ; cx = number of blocks to read
    cmp cx, 12
    ja .too_big

.read_blocks:
    mov eax, [si]                   ; block number
    test eax, eax
    jz .done_reading
    shl eax, 1                      ; LBA = block * 2
    mov [dap_lba], eax
    mov word [dap_count], SECTORS_PER_BLK

    ; Set destination in DAP (segment:offset form)
    mov [dap_dest], di
    mov [dap_dest + 2], es

    call read_disk

    add di, BLOCK_SIZE              ; advance destination
    ; Handle segment wrap if needed
    test di, di
    jnz .no_seg_wrap
    mov ax, es
    add ax, 0x1000                  ; advance segment by 64KB
    mov es, ax
.no_seg_wrap:
    add si, 4                       ; next block pointer
    dec cx
    jnz .read_blocks

.done_reading:
    ; Restore ES
    xor ax, ax
    mov es, ax

    mov si, msg_stage2_loaded
    call print

    ; ---- Get E820 memory map ----
    call get_e820_mmap

    ; ---- Enter protected mode ----
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

;-----------------------------------------------------------------------------
; get_e820_mmap - BIOS INT 15h, AX=E820h memory map
; Stores entries at E820_BUF, count in [e820_count]
;-----------------------------------------------------------------------------
get_e820_mmap:
    mov di, e820_map                ; destination for entries
    xor ebx, ebx                   ; continuation value (0 = start)
    mov [e820_count], dword 0

.e820_loop:
    mov edx, 0x534D4150             ; 'SMAP'
    mov eax, 0xE820
    mov ecx, 24                     ; request 24 bytes per entry
    mov [es:di + 20], dword 1       ; force valid ACPI 3.X entry
    int 0x15
    jc .e820_done                   ; CF set = end/error

    cmp eax, 0x534D4150             ; verify 'SMAP' in EAX
    jne .e820_done

    ; Check for zero-length entry
    mov eax, [es:di + 8]
    or eax, [es:di + 12]
    jz .e820_skip                   ; skip zero-length entries

    add di, 24
    inc dword [e820_count]
    cmp dword [e820_count], 32      ; max 32 entries
    jge .e820_done

.e820_skip:
    test ebx, ebx                   ; ebx = 0 means last entry
    jnz .e820_loop

.e820_done:
    ret

;-----------------------------------------------------------------------------
; enter_pm - Switch to 32-bit protected mode and jump to stage2
;-----------------------------------------------------------------------------
enter_pm:
    cli

    ; Enable A20 via port 0x92 (fast method)
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Load GDT
    lgdt [gdt_desc]

    ; Set PE bit
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to flush pipeline and enter 32-bit code
    jmp 0x08:pm_entry

;=============================================================================
; 32-bit Protected Mode Code
;=============================================================================
[BITS 32]
pm_entry:
    ; Setup segment registers
    mov ax, 0x10                    ; data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00090000             ; stack at 576KB

    ; Call stage2_main(e820_count, e820_map_phys, drive_number)
    ; Stage2 is at STAGE2_PHYS
    xor eax, eax
    mov al, [drive]
    push eax                        ; arg3: drive number

    push dword e820_map             ; arg2: physical addr of E820 entries
    push dword [e820_count]         ; arg1: E820 entry count

    mov eax, STAGE2_PHYS
    call eax

    ; Should not return
    cli
    hlt

;=============================================================================
; GDT (flat model)
;=============================================================================
[BITS 16]
align 8
gdt:
    ; Null descriptor
    dq 0
    ; 0x08: Code segment (base=0, limit=4GB, 32-bit, execute/read)
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    ; 0x10: Data segment (base=0, limit=4GB, 32-bit, read/write)
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt - 1            ; limit
    dd gdt                           ; base

;-----------------------------------------------------------------------------
; Stage1.5 messages and data
;-----------------------------------------------------------------------------
msg_ext2_ok:        db "ext2 OK", 13, 10, 0
msg_stage2_loaded:  db "stage2 loaded", 13, 10, 0
msg_no_ext2:        db "Not ext2", 13, 10, 0
msg_no_stage2:      db "No inode 5", 13, 10, 0
msg_too_big:        db "stage2 >12K", 13, 10, 0

e820_count:         dd 0

; Pad sector 1 to 512 bytes (total file = 1024)
times 1024 - ($ - $$) db 0

;=============================================================================
; E820 memory map buffer lives just past the boot block in BSS-like area
; Since we're running from < 0x8000 and stage2 is at 0x5000, we put
; the E820 map at 0x0500 (conventional low memory, safe before stage2)
; Actually we'll put it right here after the boot block but before stage2.
; We use an absolute address label that the assembler knows about.
;=============================================================================
; E820 entries are stored starting at fixed address 0x0500
; (this is safe; BIOS data area ends at 0x0500)
; We define the address as a constant and write to it directly
e820_map equ 0x0500
