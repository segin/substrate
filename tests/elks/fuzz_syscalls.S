.code16
.global _start

.section .text
_start:
    mov $0x1234, %di
    mov $64, %cx
    mov $66, %bp

fuzz_unsupported_loop:
    mov %bp, %ax
    mov %di, %bx
    mov %di, %dx
    add $0x1111, %dx
    mov %di, %si
    xor $0x55AA, %si
    int $0x80
    test %ax, %ax
    jns fuzz_fail
    add $3, %bp
    add $0x2221, %di
    loop fuzz_unsupported_loop

    mov $5, %ax
    mov $0xFFFF, %bx
    xor %cx, %cx
    xor %dx, %dx
    xor %si, %si
    int $0x80
    test %ax, %ax
    jns fuzz_fail

    mov $3, %ax
    mov $0xFFFF, %bx
    mov $0xFFFF, %cx
    mov $32, %dx
    xor %si, %si
    int $0x80
    test %ax, %ax
    jns fuzz_fail

    mov $4, %ax
    mov $0xFFFF, %bx
    mov $0xFFFF, %cx
    mov $32, %dx
    xor %si, %si
    int $0x80
    test %ax, %ax
    jns fuzz_fail

    mov $6, %ax
    mov $0xFFFF, %bx
    xor %cx, %cx
    xor %dx, %dx
    xor %si, %si
    int $0x80
    test %ax, %ax
    jns fuzz_fail

    mov $7, %ax
    mov $0xFFFF, %bx
    mov $0xFFFF, %cx
    xor %dx, %dx
    xor %si, %si
    int $0x80
    test %ax, %ax
    jns fuzz_fail

    mov $4, %ax
    mov $1, %bx
    mov $ok_msg, %cx
    mov $(ok_msg_end - ok_msg), %dx
    int $0x80

    xor %bx, %bx
    mov $1, %ax
    int $0x80

fuzz_fail:
    mov $4, %ax
    mov $1, %bx
    mov $fail_msg, %cx
    mov $(fail_msg_end - fail_msg), %dx
    int $0x80

    mov $1, %bx
    mov $1, %ax
    int $0x80

.section .rodata
ok_msg:
    .ascii "ELKS fuzz done\n"
ok_msg_end:
fail_msg:
    .ascii "ELKS fuzz fail\n"
fail_msg_end:
