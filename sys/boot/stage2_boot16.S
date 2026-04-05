.intel_syntax noprefix
.code16

.section .boot16.entry, "ax"
.global stage2_boot16_entry

.set BOOTLINE_BUF,    0x0800
.set BOOTLINE_MAX,    256
.set BIOS_TICK_COUNT, 0x046C
.set VBE_INFO_BUF,    0x0A00
.set VBE_MODE_BUF,    0x0C00
.set VBE_132X25,      0x0E00
.set VBE_132X43,      0x0E02
.set VBE_132X50,      0x0E04
.set VBE_132X60,      0x0E06

stage2_boot16_entry:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    push cs
    pop ds
    xor ax, ax
    mov es, ax
    call prompt_cmdline
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    lret

.section .boot16, "ax"

print:
    lodsb
    test al, al
    jz 1f
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print
1:
    ret

putc:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    ret

getc_bios:
    xor ah, ah
    int 0x16
    ret

scan_token:
    mov si, BOOTLINE_BUF
1:
    cmp byte ptr es:[si], 0
    je 5f
    cmp byte ptr es:[si], ' '
    je 4f
    mov bx, si
    push si
    push di
2:
    mov al, byte ptr [di]
    test al, al
    jz 3f
    cmp al, byte ptr es:[si]
    jne 6f
    inc si
    inc di
    jmp 2b
3:
    mov al, byte ptr es:[si]
    test al, al
    jz 7f
    cmp al, ' '
    jne 6f
7:
    pop di
    pop si
    stc
    ret
6:
    pop di
    pop si
8:
    cmp byte ptr es:[si], 0
    je 5f
    cmp byte ptr es:[si], ' '
    je 4f
    inc si
    jmp 8b
4:
    inc si
    jmp 1b
5:
    clc
    ret

append_token:
    mov di, BOOTLINE_BUF
1:
    cmp byte ptr es:[di], 0
    je 2f
    inc di
    jmp 1b
2:
    cmp di, BOOTLINE_BUF
    je 3f
    mov byte ptr es:[di], ' '
    inc di
3:
    lodsb
    stosb
    test al, al
    jnz 3b
    ret

append_textmode_bios:
    mov di, offset tok_textmode_bios
    call scan_token
    jc 1f
    mov si, offset tok_textmode_bios
    call append_token
1:
    ret

query_vbe_text_modes:
    mov word ptr es:[VBE_132X25], 0
    mov word ptr es:[VBE_132X43], 0
    mov word ptr es:[VBE_132X50], 0
    mov word ptr es:[VBE_132X60], 0

    mov di, VBE_INFO_BUF
    mov dword ptr es:[di], 0x32454256
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne 9f

    mov dx, word ptr es:[VBE_INFO_BUF + 0x10]
    mov bx, word ptr es:[VBE_INFO_BUF + 0x0E]
    test dx, dx
    jz 9f

    push ds
    mov ds, dx
    mov si, bx
1:
    lodsw
    cmp ax, 0xFFFF
    je 8f
    push si
    mov cx, ax
    xor ax, ax
    mov es, ax
    mov di, VBE_MODE_BUF
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne 7f
    mov ax, word ptr es:[VBE_MODE_BUF + 0x00]
    test ax, 0x0001
    jz 7f
    test ax, 0x0010
    jnz 7f
    mov al, byte ptr es:[VBE_MODE_BUF + 0x16]
    test al, al
    jz 7f
    mov al, byte ptr es:[VBE_MODE_BUF + 0x17]
    test al, al
    jz 7f
    mov ax, word ptr es:[VBE_MODE_BUF + 0x12]
    cmp ax, 132
    jne 7f
    mov ax, word ptr es:[VBE_MODE_BUF + 0x14]
    cmp ax, 25
    je 2f
    cmp ax, 43
    je 3f
    cmp ax, 50
    je 4f
    cmp ax, 60
    je 5f
    jmp 7f
2:
    mov word ptr es:[VBE_132X25], cx
    jmp 7f
3:
    mov word ptr es:[VBE_132X43], cx
    jmp 7f
4:
    mov word ptr es:[VBE_132X50], cx
    jmp 7f
5:
    mov word ptr es:[VBE_132X60], cx
7:
    pop si
    jmp 1b
8:
    pop ds
9:
    xor ax, ax
    mov es, ax
    push cs
    pop ds
    ret

select_80x25:
    mov ax, 0x0003
    int 0x10
    mov si, offset tok_video_text_80x25
    call append_token
    call append_textmode_bios
    ret

select_80x43:
    mov ax, 0x0003
    int 0x10
    mov ax, 0x1111
    xor bx, bx
    int 0x10
    mov si, offset tok_video_text_80x43
    call append_token
    call append_textmode_bios
    ret

select_80x50:
    mov ax, 0x0003
    int 0x10
    mov ax, 0x1112
    xor bx, bx
    int 0x10
    mov si, offset tok_video_text_80x50
    call append_token
    call append_textmode_bios
    ret

select_vbe_textmode:
    push si
    mov ax, 0x4F02
    int 0x10
    pop si
    call append_token
    call append_textmode_bios
    ret

vga_ask_menu:
    mov si, offset msg_vga_menu
    call print
    mov ax, word ptr es:[VBE_132X25]
    test ax, ax
    jz 1f
    mov si, offset msg_vga_menu_132x25
    call print
1:
    mov ax, word ptr es:[VBE_132X43]
    test ax, ax
    jz 2f
    mov si, offset msg_vga_menu_132x43
    call print
2:
    mov ax, word ptr es:[VBE_132X50]
    test ax, ax
    jz 3f
    mov si, offset msg_vga_menu_132x50
    call print
3:
    mov ax, word ptr es:[VBE_132X60]
    test ax, ax
    jz 4f
    mov si, offset msg_vga_menu_132x60
    call print
4:
    mov si, offset msg_vga_menu_prompt
    call print
5:
    call getc_bios
    cmp al, '1'
    je 10f
    cmp al, '2'
    je 11f
    cmp al, '3'
    je 12f
    cmp al, '4'
    je 13f
    cmp al, '5'
    je 14f
    cmp al, '6'
    je 15f
    cmp al, '7'
    je 16f
    jmp 5b
10:
    call putc
    mov al, 13
    call putc
    mov al, 10
    call putc
    call select_80x25
    ret
11:
    call putc
    mov al, 13
    call putc
    mov al, 10
    call putc
    call select_80x43
    ret
12:
    call putc
    mov al, 13
    call putc
    mov al, 10
    call putc
    call select_80x50
    ret
13:
    mov bx, word ptr es:[VBE_132X25]
    test bx, bx
    jz 5b
    call putc
    mov al, 13
    call putc
    mov al, 10
    call putc
    mov si, offset tok_video_text_132x25
    call select_vbe_textmode
    ret
14:
    mov bx, word ptr es:[VBE_132X43]
    test bx, bx
    jz 5b
    call putc
    mov al, 13
    call putc
    mov al, 10
    call putc
    mov si, offset tok_video_text_132x43
    call select_vbe_textmode
    ret
15:
    mov bx, word ptr es:[VBE_132X50]
    test bx, bx
    jz 5b
    call putc
    mov al, 13
    call putc
    mov al, 10
    call putc
    mov si, offset tok_video_text_132x50
    call select_vbe_textmode
    ret
16:
    mov bx, word ptr es:[VBE_132X60]
    test bx, bx
    jz 5b
    call putc
    mov al, 13
    call putc
    mov al, 10
    call putc
    mov si, offset tok_video_text_132x60
    call select_vbe_textmode
    ret

maybe_apply_video_mode:
    mov di, offset tok_vga_ask
    call scan_token
    jc 1f
    mov di, offset tok_video_ask
    call scan_token
    jc 1f
    mov di, offset tok_video_text_80x25
    call scan_token
    jc 2f
    mov di, offset tok_video_text_80x43
    call scan_token
    jc 3f
    mov di, offset tok_video_text_80x50
    call scan_token
    jc 4f
    mov di, offset tok_video_text_132x25
    call scan_token
    jc 5f
    mov di, offset tok_video_text_132x43
    call scan_token
    jc 6f
    mov di, offset tok_video_text_132x50
    call scan_token
    jc 7f
    mov di, offset tok_video_text_132x60
    call scan_token
    jc 8f
    ret
1:
    mov byte ptr es:[bx], 'x'
    call query_vbe_text_modes
    call vga_ask_menu
    ret
2:
    call select_80x25
    ret
3:
    call select_80x43
    ret
4:
    call select_80x50
    ret
5:
    call query_vbe_text_modes
    mov bx, word ptr es:[VBE_132X25]
    test bx, bx
    jz 9f
    mov si, offset tok_video_text_132x25
    call select_vbe_textmode
    ret
6:
    call query_vbe_text_modes
    mov bx, word ptr es:[VBE_132X43]
    test bx, bx
    jz 9f
    mov si, offset tok_video_text_132x43
    call select_vbe_textmode
    ret
7:
    call query_vbe_text_modes
    mov bx, word ptr es:[VBE_132X50]
    test bx, bx
    jz 9f
    mov si, offset tok_video_text_132x50
    call select_vbe_textmode
    ret
8:
    call query_vbe_text_modes
    mov bx, word ptr es:[VBE_132X60]
    test bx, bx
    jz 9f
    mov si, offset tok_video_text_132x60
    call select_vbe_textmode
9:
    ret

prompt_cmdline:
    mov byte ptr es:[BOOTLINE_BUF], 0
    mov si, offset msg_boot_prompt
    call print

    mov di, BOOTLINE_BUF
    mov bx, word ptr es:[BIOS_TICK_COUNT]
1:
    mov ah, 0x01
    int 0x16
    jnz 2f
    mov ax, word ptr es:[BIOS_TICK_COUNT]
    sub ax, bx
    cmp ax, 91
    jb 1b
    mov si, offset msg_default_kernel
    call print
    ret
2:
    call getc_bios
    cmp al, 13
    je 5f
    cmp al, 8
    je 4f
    cmp al, 32
    jb 2b
    cmp di, BOOTLINE_BUF + BOOTLINE_MAX - 1
    jae 2b
    stosb
    call putc
    jmp 2b
4:
    cmp di, BOOTLINE_BUF
    je 2b
    dec di
    mov al, 8
    call putc
    mov al, ' '
    call putc
    mov al, 8
    call putc
    jmp 2b
5:
    mov byte ptr es:[di], 0
    mov al, 13
    call putc
    mov al, 10
    call putc
    cmp di, BOOTLINE_BUF
    je 6f
    call maybe_apply_video_mode
6:
    ret

msg_boot_prompt:
    .asciz "boot: "
msg_default_kernel:
    .asciz "vmunix\r\n"
msg_vga_menu:
    .asciz "\r\nVGA text\r\n1.80x25\r\n2.80x43\r\n3.80x50\r\n"
msg_vga_menu_132x25:
    .asciz "4.132x25\r\n"
msg_vga_menu_132x43:
    .asciz "5.132x43\r\n"
msg_vga_menu_132x50:
    .asciz "6.132x50\r\n"
msg_vga_menu_132x60:
    .asciz "7.132x60\r\n"
msg_vga_menu_prompt:
    .asciz "> "
tok_vga_ask:
    .asciz "vga=ask"
tok_video_ask:
    .asciz "video=ask"
tok_video_text_80x25:
    .asciz "video=text:80x25"
tok_video_text_80x43:
    .asciz "video=text:80x43"
tok_video_text_80x50:
    .asciz "video=text:80x50"
tok_video_text_132x25:
    .asciz "video=text:132x25"
tok_video_text_132x43:
    .asciz "video=text:132x43"
tok_video_text_132x50:
    .asciz "video=text:132x50"
tok_video_text_132x60:
    .asciz "video=text:132x60"
tok_textmode_bios:
    .asciz "textmode_bios=1"
