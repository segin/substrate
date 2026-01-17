/*
 * syscall.S - i386 raw syscall entry point
 *
 * Provides syscall() function for Substrate i386.
 * Uses int $0x80 convention with args in ebx, ecx, edx, esi, edi, ebp.
 */

.global syscall
.type syscall, @function

syscall:
    push %ebp
    mov  %esp, %ebp
    push %ebx
    push %esi
    push %edi

    /* Load syscall number and args */
    mov  8(%ebp), %eax      /* syscall number */
    mov  12(%ebp), %ebx     /* arg1 */
    mov  16(%ebp), %ecx     /* arg2 */
    mov  20(%ebp), %edx     /* arg3 */
    mov  24(%ebp), %esi     /* arg4 */
    mov  28(%ebp), %edi     /* arg5 */
    mov  32(%ebp), %ebp     /* arg6 - clobbers frame pointer */

    int  $0x80

    pop  %edi
    pop  %esi
    pop  %ebx
    pop  %ebp
    ret

.size syscall, . - syscall
