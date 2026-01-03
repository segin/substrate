.global _syscall0
.global _syscall1
.global _syscall2
.global _syscall3
.global _syscall4
.global _syscall5
.global _syscall6

# int _syscall0(int num)
_syscall0:
    push %ebx
    mov 8(%esp), %eax
    int $0x80
    pop %ebx
    ret

# int _syscall1(int num, int arg1)
_syscall1:
    push %ebx
    mov 8(%esp), %eax
    mov 12(%esp), %ebx
    int $0x80
    pop %ebx
    ret

# int _syscall2(int num, int arg1, int arg2)
_syscall2:
    push %ebx
    push %ecx
    mov 12(%esp), %eax
    mov 16(%esp), %ebx
    mov 20(%esp), %ecx
    int $0x80
    pop %ecx
    pop %ebx
    ret

# int _syscall3(int num, int arg1, int arg2, int arg3)
_syscall3:
    push %ebx
    push %ecx
    push %edx
    mov 16(%esp), %eax
    mov 20(%esp), %ebx
    mov 24(%esp), %ecx
    mov 28(%esp), %edx
    int $0x80
    pop %edx
    pop %ecx
    pop %ebx
    ret

# int _syscall4(int num, int arg1, int arg2, int arg3, int arg4)
_syscall4:
    push %ebx
    push %ecx
    push %edx
    push %esi
    mov 20(%esp), %eax
    mov 24(%esp), %ebx
    mov 28(%esp), %ecx
    mov 32(%esp), %edx
    mov 36(%esp), %esi
    int $0x80
    pop %esi
    pop %edx
    pop %ecx
    pop %ebx
    ret

# int _syscall5(int num, int arg1, int arg2, int arg3, int arg4, int arg5)
_syscall5:
    push %ebx
    push %ecx
    push %edx
    push %esi
    push %edi
    mov 24(%esp), %eax
    mov 28(%esp), %ebx
    mov 32(%esp), %ecx
    mov 36(%esp), %edx
    mov 40(%esp), %esi
    mov 44(%esp), %edi
    int $0x80
    pop %edi
    pop %esi
    pop %edx
    pop %ecx
    pop %ebx
    ret