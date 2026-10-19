/*
 * ds_span — a syscall buffer that starts inside the data segment but runs
 * past its end is refused with EFAULT.
 *
 * The personality checked only a buffer's first byte, so TCGETS copied a
 * termios into whatever lay past the segment (in a combined image, the
 * unmapped far-text window: a kernel page fault), and write() read past it.
 *
 * Exit status: 0 = both refused; 1 = ioctl(TCGETS) was not refused;
 * 2 = write() was not refused.
 */
.code16
.global _start

.equ EFAULT, 14
.equ BUF, 0xffe0                /* in the segment; +36 / +256 bytes is not */

_start:
    /* ioctl(1, TCGETS, BUF) */
    mov $54, %ax
    mov $1, %bx
    mov $0x5401, %cx
    mov $BUF, %dx
    int $0x80
    cmp $-EFAULT, %ax
    je 1f
    mov $1, %bx
    jmp out
1:
    /* write(1, BUF, 256) */
    mov $4, %ax
    mov $1, %bx
    mov $BUF, %cx
    mov $256, %dx
    int $0x80
    cmp $-EFAULT, %ax
    je 2f
    mov $2, %bx
    jmp out
2:
    xor %bx, %bx
out:
    mov $1, %ax
    int $0x80
