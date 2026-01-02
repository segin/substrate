# uname - Linux/i386
# Build: as --32 -o uname_linux.o uname_linux.S && ld -m elf_i386 -o uname_linux uname_linux.o

.section .bss
    .lcomm buf, 325             # struct utsname (65*5)

.section .data
    sp: .asciz " "
    nl: .asciz "\n"
    os: .asciz "GNU/Linux"

.section .text
.global _start

_start:
    mov (%esp), %ecx            # argc
    lea 4(%esp), %ebx           # argv
    
    # Default: -s if no args
    xor %edi, %edi              # flags
    cmp $1, %ecx
    jle .Ldefault
    
    # Parse argv[1]
    mov 4(%ebx), %esi
    cmpb $'-', (%esi)
    jne .Ldefault
    inc %esi
.Lflags:
    movb (%esi), %al
    test %al, %al
    jz .Ldone_parse
    cmpb $'a', %al
    je .Lall
    cmpb $'s', %al
    je .Ls
    cmpb $'n', %al
    je .Ln
    cmpb $'r', %al
    je .Lr
    cmpb $'v', %al
    je .Lv
    cmpb $'m', %al
    je .Lm
    cmpb $'o', %al
    je .Lo
    inc %esi
    jmp .Lflags

.Lall:  or $0x3F, %edi
        jmp .Lnext
.Ls:    or $0x01, %edi
        jmp .Lnext
.Ln:    or $0x02, %edi
        jmp .Lnext
.Lr:    or $0x04, %edi
        jmp .Lnext
.Lv:    or $0x08, %edi
        jmp .Lnext
.Lm:    or $0x10, %edi
        jmp .Lnext
.Lo:    or $0x20, %edi
.Lnext: inc %esi
        jmp .Lflags

.Ldefault:
    or $0x01, %edi

.Ldone_parse:
    # uname(buf)
    mov $122, %eax
    mov $buf, %ebx
    int $0x80
    test %eax, %eax
    js .Lexit1

    xor %ebp, %ebp              # first=0
    
    test $0x01, %edi            # -s
    jz .L1
    mov $buf, %esi
    call .Lprint
.L1:
    test $0x02, %edi            # -n
    jz .L2
    mov $buf+65, %esi
    call .Lprint
.L2:
    test $0x04, %edi            # -r
    jz .L3
    mov $buf+130, %esi
    call .Lprint
.L3:
    test $0x08, %edi            # -v
    jz .L4
    mov $buf+195, %esi
    call .Lprint
.L4:
    test $0x10, %edi            # -m
    jz .L5
    mov $buf+260, %esi
    call .Lprint
.L5:
    test $0x20, %edi            # -o
    jz .L6
    mov $os, %esi
    call .Lprint
.L6:
    # newline
    mov $4, %eax
    mov $1, %ebx
    mov $nl, %ecx
    mov $1, %edx
    int $0x80
    
    xor %ebx, %ebx
.Lexit:
    mov $1, %eax
    int $0x80

.Lexit1:
    mov $1, %ebx
    jmp .Lexit

# Print string at %esi, prepend space if not first
.Lprint:
    test %ebp, %ebp
    jz .Lnosp
    push %edi
    mov $4, %eax
    mov $1, %ebx
    mov $sp, %ecx
    mov $1, %edx
    int $0x80
    pop %edi
.Lnosp:
    inc %ebp
    # strlen
    mov %esi, %ecx
    xor %edx, %edx
.Llen:  cmpb $0, (%ecx,%edx)
        jz .Lgot
        inc %edx
        jmp .Llen
.Lgot:
    mov $4, %eax
    mov $1, %ebx
    mov %esi, %ecx
    int $0x80
    ret
