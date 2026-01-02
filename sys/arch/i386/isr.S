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
ISR_NOERRCODE 44
ISR_NOERRCODE 128

.extern isr_handler
.extern syscall_handler

isr_common_stub:
    pusha
    mov %ds, %ax
    push %eax
    
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    push %esp      # Push pointer to registers_t
    
    # Check int_no (offset 44 from current esp: ptr(4) + ds(4) + pusha(32) + int_no(4) = 44?)
    # Let's trace stack:
    # [SS, UserESP, EFLAGS, CS, EIP] (Hardware)
    # [ErrCode, IntNo] (ISR Macro)
    # [EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI] (Pusha)
    # [DS] (Manual push)
    # [Ptr to Stack] (Manual push) <- ESP
    
    # Registers_t struct matches: DS, EDI...EAX, IntNo, ErrCode, EIP... 
    
    # So regs->int_no is at offset: 
    # DS (4) + Pusha (32) = 36 bytes from the structure base.
    # Structure base is at (%esp) + 4 (skipping the ptr push).
    
    mov 40(%esp), %eax   # 4 (ptr) + 4 (ds) + 32 (pusha) = 40? No.
    # regs is at esp+4.
    # regs->int_no is at offset 36 in regs.
    # So 4 + 36 = 40.
    
    cmp $128, %eax
    je call_syscall
    
    call isr_handler
    jmp isr_exit

call_syscall:
    call syscall_handler

isr_exit:
    add $4, %esp   # Pop pointer to regs
    
    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    popa
    add $8, %esp # Cleans up the pushed error code and ISR number
    iret

# Jump to user mode
# void jump_to_userspace(uint32_t entry_point, uint32_t user_stack)
# User code selector: 0x1B (0x18 | 3)
# User data selector: 0x23 (0x20 | 3)
.global jump_to_userspace
jump_to_userspace:
    # Turn off interrupts during transition
    cli
    
    # Get parameters
    mov 4(%esp), %ecx   # entry_point
    mov 8(%esp), %edx   # user_stack
    
    # Build IRET stack frame (in order pushed):
    # SS, ESP, EFLAGS, CS, EIP
    push $0x23          # User SS
    push %edx           # User ESP
    pushf               # EFLAGS
    pop %eax
    or $0x200, %eax     # Enable interrupts in user mode
    push %eax
    push $0x1B          # User CS
    push %ecx           # User EIP
    
    # Set up segment registers for user mode RIGHT before IRET
    # This must be done AFTER building the IRET frame
    mov $0x23, %ax      # User data selector
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    # Jump to user mode
    iret
# fork_child_return - called when child process is scheduled
# Child's stack already has: DS, pusha regs, int_no, err_code, IRET frame
# This restores registers and returns to userspace via IRET
.global fork_child_return
fork_child_return:
    # Pop DS and restore data segments
    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    # Restore general registers (popa)
    popa
    
    # Skip int_no and err_code
    add $8, %esp
    
    # Return to userspace via IRET
    # Stack has: EIP, CS, EFLAGS, UserESP, SS
    iret
