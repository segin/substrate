.code16
.global _start

_start:
    mov $8, %ax
    mov $path, %bx
    mov $0644, %cx
    int $0x80
    test %ax, %ax
    js fail
    mov %ax, fd

    mov $4, %ax
    mov fd, %bx
    mov $payload, %cx
    mov $(payload_end - payload), %dx
    int $0x80
    cmp $(payload_end - payload), %ax
    jne fail

    mov $6, %ax
    mov fd, %bx
    int $0x80

    mov $5, %ax
    mov $path, %bx
    xor %cx, %cx
    xor %dx, %dx
    int $0x80
    test %ax, %ax
    js fail
    mov %ax, fd

    mov $3, %ax
    mov fd, %bx
    mov $buffer, %cx
    mov $(payload_end - payload), %dx
    int $0x80
    cmp $(payload_end - payload), %ax
    jne fail

    mov $6, %ax
    mov fd, %bx
    int $0x80

    mov $(payload_end - payload), %cx
    mov $payload, %si
    mov $buffer, %di
compare_loop:
    lodsb
    cmp %al, (%di)
    jne fail
    inc %di
    loop compare_loop

    mov $10, %ax
    mov $path, %bx
    int $0x80
    test %ax, %ax
    js fail

    mov $4, %ax
    mov $1, %bx
    mov $buffer, %cx
    mov $(payload_end - payload), %dx
    int $0x80

    xor %bx, %bx
    mov $1, %ax
    int $0x80

fail:
    mov $1, %bx
    mov $1, %ax
    int $0x80

.align 2
fd:
    .word 0
path:
    .ascii "elksio.tmp\0"
payload:
    .ascii "ELKS file io\n"
payload_end:
buffer:
    .space 13
