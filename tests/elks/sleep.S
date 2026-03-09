.code16
.global _start

_start:
    mov $48, %ax
    mov $14, %bx
    mov $alarm_handler, %cx
    mov %cs, %dx
    int $0x80

    mov $27, %ax
    mov $1, %bx
    int $0x80

    mov $29, %ax
    int $0x80

    mov $4, %ax
    mov $1, %bx
    mov $msg, %cx
    mov $(msg_end - msg), %dx
    int $0x80

    xor %bx, %bx
    mov $1, %ax
    int $0x80

alarm_handler:
    retf $2

msg:
    .ascii "Slept, ELKS!\n"
msg_end:
