.code16
.global _start

_start:
    mov $4, %ax
    mov $1, %bx
    mov $msg, %cx
    mov $(msg_end - msg), %dx
    int $0x80

    xor %bx, %bx
    mov $1, %ax
    int $0x80

msg:
    .ascii "Hello, ELKS!\n"
msg_end:
