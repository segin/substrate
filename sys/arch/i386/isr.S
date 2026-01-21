.global idt_flush
idt_flush:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    jmp $0x08, $.flush
.flush:
    ret

.global tss_flush
tss_flush:
    mov $0x28, %ax
    ltr %ax
    ret

.global switch_to
switch_to:
    # switch_to(thread_t *prev, thread_t *next)
    # [ESP]   Return Address
    # [ESP+4] prev
    # [ESP+8] next

    # Save callee-saved registers
    push %ebp
    push %edi
    push %esi
    push %ebx

    # Load prev and next pointers
    # Offset is now +16 due to 4 pushes
    mov 20(%esp), %eax # eax = prev
    mov 24(%esp), %edx # edx = next

    # Save current stack pointer
    mov %esp, 8(%eax)  # prev->kstack_ptr = esp (assuming offset 8)

    # Switch to next stack pointer
    mov 8(%edx), %esp  # esp = next->kstack_ptr

    # Restore callee-saved registers
    pop %ebx
    pop %esi
    pop %edi
    pop %ebp

    ret

.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    cli
    push $0
    push $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    cli
    push $\num
    jmp isr_common_stub
.endm

// Define first 32 exceptions
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

ISR_NOERRCODE 32
ISR_NOERRCODE 33
ISR_NOERRCODE 34
ISR_NOERRCODE 35
ISR_NOERRCODE 36
ISR_NOERRCODE 37
ISR_NOERRCODE 38
ISR_NOERRCODE 39
ISR_NOERRCODE 40
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44
ISR_NOERRCODE 45
ISR_NOERRCODE 46
ISR_NOERRCODE 47
ISR_NOERRCODE 128

.extern isr_handler
.extern syscall_handler

isr_common_stub:
    pusha
    
    # Save segment registers
    mov %ds, %ax
    push %eax          # Save DS
    mov %es, %ax
    push %eax          # Save ES
    mov %fs, %ax
    push %eax          # Save FS
    mov %gs, %ax
    push %eax          # Save GS
    
    # Load kernel data segment
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    push %esp          # Push pointer to registers_t
    
    # Stack layout now:
    # [SS, UserESP, EFLAGS, CS, EIP] (Hardware) 
    # [ErrCode, IntNo] (ISR Macro)
    # [EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI] (Pusha = 32 bytes)
    # [DS] (4 bytes)
    # [ES] (4 bytes)
    # [FS] (4 bytes)
    # [GS] (4 bytes)
    # [Ptr to Stack] <- ESP (4 bytes)
    
    # int_no offset: 4 (ptr) + 4 (gs) + 4 (fs) + 4 (es) + 4 (ds) + 32 (pusha) = 52
    mov 52(%esp), %eax
    
    cmp $128, %eax
    je call_syscall
    
    call isr_handler
    jmp isr_exit

call_syscall:
    call syscall_handler

isr_exit:
    add $4, %esp       # Pop pointer to regs
    
    pop %eax           # Restore GS
    mov %ax, %gs
    pop %eax           # Restore FS
    mov %ax, %fs
    pop %eax           # Restore ES
    mov %ax, %es
    pop %eax           # Restore DS
    mov %ax, %ds
    
    popa
    add $8, %esp       # Clean up error code and ISR number
    iret

# Jump to user mode
# void jump_to_userspace(uint32_t entry_point, uint32_t user_stack)
# User code selector: 0x1B (0x18 | 3)
# User data selector: 0x23 (0x20 | 3)
.global jump_to_userspace
jump_to_userspace:
    # Load parameters from GLOBAL variables
    # This bypasses stack parameter passing completely
    mov 4(%esp), %ecx
    mov 8(%esp), %edx
    
    # NOW turn off interrupts
    cli
    
    # Set DS/ES/FS to user data segment, GS to TLS segment
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es  
    mov %ax, %fs
    mov $0x33, %ax    # TLS segment (GDT entry 6 | RPL 3)
    mov %ax, %gs
    
    # Build IRET stack frame (in order pushed):
    # SS, ESP, EFLAGS, CS, EIP
    push $0x23          # User SS
    push %edx           # User ESP (loaded earlier)
    pushf               # EFLAGS
    pop %eax
    
    # or $0x200, %eax     # Enable interrupts in user mode
    # and $~0x200, %eax   # Force Disable
    or $0x200, %eax       # Enable Interrupts
    
    push %eax
    push $0x1B          # User CS
    push %ecx           # User EIP (loaded earlier)
    
    # Jump to user mode
    iret

# fork_child_return - called when child process is scheduled
# Child's stack already has: GS, FS, ES, DS, pusha regs, int_no, err_code, IRET frame
# This restores registers and returns to userspace via IRET
.global fork_child_return
fork_child_return:
    # Pop segment registers
    pop %eax
    mov %ax, %gs
    pop %eax
    mov %ax, %fs
    pop %eax
    mov %ax, %es
    pop %eax
    mov %ax, %ds
    
    # Restore general registers (popa)
    popa
    
    # Skip int_no and err_code
    add $8, %esp
    
    # Return to userspace via IRET
    # Stack has: EIP, CS, EFLAGS, UserESP, SS
    iret
