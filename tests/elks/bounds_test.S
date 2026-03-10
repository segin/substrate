.code16
.global _start

_start:
    mov $0xfffe, %bx
    mov (%bx), %al

    xor %bx, %bx
    mov $1, %ax
    int $0x80
