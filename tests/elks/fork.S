.code16
.global _start

_start:
    mov $2, %ax
    int $0x80
    test %ax, %ax
    js fail
    jz child

parent:
    mov %ax, child_pid
    mov $7, %ax
    mov child_pid, %bx
    mov $status_word, %cx
    xor %dx, %dx
    int $0x80
    test %ax, %ax
    js fail

    mov $4, %ax
    mov $1, %bx
    mov $parent_msg, %cx
    mov $(parent_msg_end - parent_msg), %dx
    int $0x80

    xor %bx, %bx
    mov $1, %ax
    int $0x80

child:
    mov $4, %ax
    mov $1, %bx
    mov $child_msg, %cx
    mov $(child_msg_end - child_msg), %dx
    int $0x80

    xor %bx, %bx
    mov $1, %ax
    int $0x80

fail:
    mov $4, %ax
    mov $1, %bx
    mov $fail_msg, %cx
    mov $(fail_msg_end - fail_msg), %dx
    int $0x80

    mov $1, %bx
    mov $1, %ax
    int $0x80

.align 2
child_pid:
    .word 0
status_word:
    .long 0

child_msg:
    .ascii "ELKS child\n"
child_msg_end:
parent_msg:
    .ascii "ELKS parent\n"
parent_msg_end:
fail_msg:
    .ascii "ELKS fork fail\n"
fail_msg_end:
