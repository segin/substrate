# uname - FreeBSD/i386
# Build: as --32 -o uname-freebsd.o uname-freebsd.S && ld -m elf_i386 -o uname-freebsd uname-freebsd.o
# FreeBSD uses int $0x80 with syscall number in %eax, args pushed right-to-left on stack

.section .bss
    .lcomm buf, 1280            # struct utsname (256*5)

.section .data
    sp: .asciz " "
    nl: .asciz "\n"
    os: .asciz "FreeBSD"

# FreeBSD utsname offsets (256 bytes each)
.equ OFF_SYSNAME,  0
.equ OFF_NODENAME, 256
.equ OFF_RELEASE,  512
.equ OFF_VERSION,  768
.equ OFF_MACHINE,  1024

.section .note.tag, "a"
    .align 4
    .long 8                     # namesz
    .long 4                     # descsz
    .long 1                     # type (NT_FREEBSD_ABI_TAG)
    .asciz "FreeBSD"            # name
    .long 1400000               # desc (__FreeBSD_version)

.section .text
.global _start

# FreeBSD syscall numbers
.equ SYS_exit,  1
.equ SYS_write, 4
.equ SYS_uname, 164

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
    cmpb $'i', %al              # FreeBSD: -i for ident
    je .Li
    cmpb $'o', %al
    je .Lo
    inc %esi
    jmp .Lflags

.Lall:  or $0x7F, %edi
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
.Li:    or $0x20, %edi
        jmp .Lnext
.Lo:    or $0x40, %edi
.Lnext: inc %esi
        jmp .Lflags

.Ldefault:
    or $0x01, %edi

.Ldone_parse:
    # FreeBSD: uname(buf) - push arg, then syscall
    push $buf
    mov $SYS_uname, %eax
    push %eax                   # dummy return address
    int $0x80
    add $8, %esp
    jc .Lexit1

    xor %ebp, %ebp              # first=0
    
    test $0x01, %edi            # -s
    jz .L1
    mov $buf+OFF_SYSNAME, %esi
    call .Lprint
.L1:
    test $0x02, %edi            # -n
    jz .L2
    mov $buf+OFF_NODENAME, %esi
    call .Lprint
.L2:
    test $0x04, %edi            # -r
    jz .L3
    mov $buf+OFF_RELEASE, %esi
    call .Lprint
.L3:
    test $0x08, %edi            # -v
    jz .L4
    mov $buf+OFF_VERSION, %esi
    call .Lprint
.L4:
    test $0x10, %edi            # -m
    jz .L5
    mov $buf+OFF_MACHINE, %esi
    call .Lprint
.L5:
    test $0x20, %edi            # -i (FreeBSD: same as -m typically)
    jz .L6
    mov $buf+OFF_MACHINE, %esi
    call .Lprint
.L6:
    test $0x40, %edi            # -o
    jz .L7
    mov $os, %esi
    call .Lprint
.L7:
    # newline - FreeBSD syscall convention
    push $1                     # len
    push $nl                    # buf
    push $1                     # fd
    mov $SYS_write, %eax
    push %eax
    int $0x80
    add $16, %esp
    
    xor %ebx, %ebx
.Lexit:
    push %ebx
    mov $SYS_exit, %eax
    push %eax
    int $0x80

.Lexit1:
    mov $1, %ebx
    jmp .Lexit

# Print string at %esi, prepend space if not first
.Lprint:
    test %ebp, %ebp
    jz .Lnosp
    push %edi
    push $1
    push $sp
    push $1
    mov $SYS_write, %eax
    push %eax
    int $0x80
    add $16, %esp
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
    push %edx                   # len
    push %esi                   # buf
    push $1                     # fd
    mov $SYS_write, %eax
    push %eax
    int $0x80
    add $16, %esp
    ret
