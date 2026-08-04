# Substrate Native Personality Syscall Interface
# Uses stack-based ABI (like FreeBSD), NOT Linux register ABI
#
# Native ABI: syscall number in EAX, arguments on stack
# Stack layout at int $0x80:
#   [esp+0]  = dummy return address (required by kernel)
#   [esp+4]  = arg1
#   [esp+8]  = arg2
#   [esp+12] = arg3
#   ...

.global _syscall0
.global _syscall1
.global _syscall2
.global _syscall3
.global _syscall4
.global _syscall4_ll
.global _syscall5
.global _syscall6

# int _syscall0(int num)
# Stack on entry: [ret] [num]
# Need: [dummy_ret] on stack, num in EAX
_syscall0:
    mov 4(%esp), %eax       # syscall number
    push $0                 # dummy return address
    int $0x80
    cdq                     # sign-extend eax into edx for int64_t return
    add $4, %esp            # pop dummy
    ret

# int _syscall1(int num, int arg1)
# Stack on entry: [ret] [num] [arg1]
_syscall1:
    mov 4(%esp), %eax       # syscall number
    push 8(%esp)            # arg1 (push before dummy)
    push $0                 # dummy return address
    int $0x80
    cdq                     # sign-extend eax into edx for int64_t return
    add $8, %esp            # pop dummy + arg1
    ret

# int _syscall2(int num, int arg1, int arg2)
# Stack on entry: [ret] [num] [arg1] [arg2]
_syscall2:
    mov 4(%esp), %eax       # syscall number
    push 12(%esp)           # arg2
    push 12(%esp)           # arg1 (offset changes after push)
    push $0                 # dummy return address
    int $0x80
    cdq                     # sign-extend eax into edx for int64_t return
    add $12, %esp           # pop dummy + args
    ret

# int _syscall3(int num, int arg1, int arg2, int arg3)
# Stack on entry: [ret] [num] [arg1] [arg2] [arg3]
_syscall3:
    mov 4(%esp), %eax       # syscall number
    push 16(%esp)           # arg3
    push 16(%esp)           # arg2
    push 16(%esp)           # arg1
    push $0                 # dummy return address
    int $0x80
    cdq                     # sign-extend eax into edx for int64_t return
    add $16, %esp           # pop dummy + args
    ret

# int _syscall4(int num, int arg1, int arg2, int arg3, int arg4)
# Stack on entry: [ret] [num] [arg1] [arg2] [arg3] [arg4]
_syscall4:
    mov 4(%esp), %eax       # syscall number
    push 20(%esp)           # arg4
    push 20(%esp)           # arg3
    push 20(%esp)           # arg2
    push 20(%esp)           # arg1
    push $0                 # dummy return address
    int $0x80
    cdq                     # sign-extend eax into edx for int64_t return
    add $20, %esp           # pop dummy + args
    ret

# int64_t _syscall4_ll(int num, int arg1, int arg2, int arg3, int arg4)
#
# Same as _syscall4 but WITHOUT the cdq: the kernel's EDX is the real high
# half of a 64-bit return, so sign-extending EAX over it would destroy the
# value.  Only for syscalls the kernel genuinely returns 64 bits from --
# see the edx_is_retval1 list in sys/arch/i386/syscall.c.  Those return
# negative errnos as a full int64, so EDX is already 0xFFFFFFFF on error
# and the caller's `r < 0` test still works.
#
# Every other syscall must keep using _syscall4: for those the kernel
# forces EDX to 0, so a 32-bit negative errno would read back as a large
# positive int64 and the error would go unnoticed.  That is what the cdq
# is for, and why this cannot simply replace it.
_syscall4_ll:
    mov 4(%esp), %eax       # syscall number
    push 20(%esp)           # arg4
    push 20(%esp)           # arg3
    push 20(%esp)           # arg2
    push 20(%esp)           # arg1
    push $0                 # dummy return address
    int $0x80
    add $20, %esp           # pop dummy + args (edx:eax preserved)
    ret

# int _syscall5(int num, int arg1, int arg2, int arg3, int arg4, int arg5)
# Stack on entry: [ret] [num] [arg1] [arg2] [arg3] [arg4] [arg5]
_syscall5:
    mov 4(%esp), %eax       # syscall number
    push 24(%esp)           # arg5
    push 24(%esp)           # arg4
    push 24(%esp)           # arg3
    push 24(%esp)           # arg2
    push 24(%esp)           # arg1
    push $0                 # dummy return address
    int $0x80
    cdq                     # sign-extend eax into edx for int64_t return
    add $24, %esp           # pop dummy + args
    ret

# int _syscall6(int num, int arg1, arg2, arg3, arg4, arg5, arg6)
# Stack on entry: [ret] [num] [arg1] [arg2] [arg3] [arg4] [arg5] [arg6]
_syscall6:
    mov 4(%esp), %eax       # syscall number
    push 28(%esp)           # arg6
    push 28(%esp)           # arg5
    push 28(%esp)           # arg4
    push 28(%esp)           # arg3
    push 28(%esp)           # arg2
    push 28(%esp)           # arg1
    push $0                 # dummy return address
    int $0x80
    cdq                     # sign-extend eax into edx for int64_t return
    add $28, %esp           # pop dummy + args
    ret

/* No executable stack required — see lib/c/arch/i386/crt0.S. */
    .section .note.GNU-stack,"",@progbits
