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

# Dynamic interrupt vectors 0x50..0xBF (MSI / MSI-X, LAPIC-delivered).
# Each needs its own stub to push the correct vector number, since the CPU
# does not push a vector for these.  isr_handler routes this range to
# irq_dispatch() and issues a LAPIC EOI (not a PIC EOI).  Generated with
# altmacro so a single .rept emits the stubs.  Vector 0x80 is the syscall
# gate (isr128, already defined above), so the ranges skip it.
.altmacro
.set msivec, 0x50
.rept (0x7F - 0x50 + 1)          # 0x50 .. 0x7F
    ISR_NOERRCODE %msivec
    .set msivec, msivec + 1
.endr
.set msivec, 0x81
.rept (0xBF - 0x81 + 1)          # 0x81 .. 0xBF (0x80 = syscall, skipped)
    ISR_NOERRCODE %msivec
    .set msivec, msivec + 1
.endr
.noaltmacro

# Table of all 112 stub entry points (0x50..0xBF) so idt_init() can install
# their gates in a loop, indexed by (vector - 0x50).  The 0x80 slot references
# the pre-existing syscall stub isr128; idt_init() skips installing a gate
# there so the syscall vector keeps its user-accessible gate.
.macro MSISTUBPTR n
    .long isr\n
.endm
.section .rodata
.align 4
.global msi_isr_stubs
msi_isr_stubs:
.altmacro
.set msivec, 0x50
.rept (0xBF - 0x50 + 1)
    MSISTUBPTR %msivec
    .set msivec, msivec + 1
.endr
.noaltmacro
.text

# SMP IPI vectors that route through the common stub -> isr_handler, which
# dispatches them to their C handlers (each sends its own LAPIC EOI):
#   0xFD (253) SCHED_IPI_VECTOR     -> sched_ipi_handler
#   0xFE (254) TLB_SHOOTDOWN_VECTOR -> pmap_shootdown_handler
ISR_NOERRCODE 253
ISR_NOERRCODE 254

# Panic IPI (vector 0xFB).  When one CPU panics, it sends this IPI
# to every other CPU.  The handler does NOT return — it disables
# interrupts and halts forever.  No common-stub round-trip, no LAPIC
# EOI: once we're here, the system is dead.
.global isr_panic_ipi
isr_panic_ipi:
    cli
1:  hlt
    jmp 1b

# LAPIC spurious interrupt (vector 0xFF).
#
# [USB-HW-03] lapic_enable(0xFF) programs this vector into the SVR on the BSP
# (main.c) and on every AP (smp_ap_entry), but idt_init() never installed a
# gate for it -- the entry stayed zeroed by the initial memset, i.e. NOT
# PRESENT.  A spurious interrupt therefore took #GP instead of being ignored.
# Real hardware raises these (that is what the vector is FOR: a level-triggered
# IRQ deasserted between the CPU accepting it and reading the vector); QEMU
# essentially never does, which is why this only ever bit on the Lenovo.
#
# Per the SDM a spurious interrupt must NOT be EOI'd, so this deliberately
# bypasses the common stub: count it and iret.  `lock incl` touches only
# EFLAGS, which iret restores.
.global isr_spurious
isr_spurious:
    lock incl lapic_spurious_count
    iret

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
# void jump_to_userspace(uint32_t entry_point, uint32_t user_stack,
#                        uint32_t entry_ebx)
# User code selector: 0x1B (0x18 | 3)
# User data selector: 0x23 (0x20 | 3)
#
# entry_ebx is the value to load into %ebx at process entry.  Most
# personalities pass 0; NetBSD/OpenBSD pass &ps_strings here because
# their crt0 reads ps_strings from %ebx (lib/csu/arch/i386/crt0.S).
.global jump_to_userspace
jump_to_userspace:
    # Load cdecl parameters:
    #   [esp+4]  = entry_point
    #   [esp+8]  = user_stack
    #   [esp+12] = entry_ebx
    mov 4(%esp),  %ecx          # %ecx = entry
    mov 8(%esp),  %edx          # %edx = user_stack (temp; restored to 0 below)
    mov 12(%esp), %ebx          # %ebx = ps_strings (or 0)

    # NOW turn off interrupts
    cli

    # A freshly exec'd image must not inherit stale TLS selectors from the
    # previous process image. i386 Linux starts with DS/ES flat, but FS/GS
    # cleared until userland installs TLS explicitly via set_thread_area.
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs

    # Build IRET stack frame (in order pushed):
    # SS, ESP, EFLAGS, CS, EIP
    push $0x23          # User SS
    push %edx           # User ESP (the user_stack arg)
    pushf               # EFLAGS
    pop %eax

    # or $0x200, %eax     # Enable interrupts in user mode
    # and $~0x200, %eax   # Force Disable
    or $0x200, %eax       # Enable Interrupts

    push %eax
    push $0x1B          # User CS
    push %ecx           # User EIP (loaded earlier)

    # A freshly exec'd image must not inherit arbitrary general-purpose
    # register contents from the previous process image.
    # Linux i386 only defines %edx = rtld_fini on entry; kernel-launched
    # processes provide NULL there unless a dynamic linker arranged otherwise.
    # %ebx is preserved (set above) so NetBSD's crt0 sees ps_strings.
    xor %eax, %eax
    xor %ecx, %ecx
    xor %edx, %edx
    xor %esi, %esi
    xor %edi, %edi
    xor %ebp, %ebp

    # Jump to user mode
    iret

# void jump_to_elks(uint32_t entry_point, uint32_t user_stack, uint32_t cs,
#                   uint32_t ds, uint32_t ss, uint32_t es, uint32_t stack_size)
.global jump_to_elks
jump_to_elks:
    # [esp+4] = entry_point
    # [esp+8] = user_stack
    # [esp+12] = cs_selector
    # [esp+16] = ds_selector
    # [esp+20] = ss_selector
    # [esp+24] = es_selector
    # [esp+28] = stack_size
    
    mov 4(%esp), %ecx   # entry_point
    mov 8(%esp), %edx   # user_stack
    mov 12(%esp), %eax  # cs_selector
    mov 16(%esp), %ebx  # ds_selector
    mov 20(%esp), %esi  # ss_selector
    mov 24(%esp), %edi  # es_selector
    mov 28(%esp), %ebp  # stack_size (ELKS expects DX)
    
    cli
    
    # Set data segments
    mov %bx, %ds
    mov %di, %es
    mov %bx, %fs
    mov %bx, %gs
    
    # Build IRET frame
    push %esi           # User SS
    push %edx           # User ESP
    pushf
    pop %edx
    or $0x200, %edx     # Enable interrupts
    push %edx           # EFLAGS
    push %eax           # User CS
    push %ecx           # User EIP
    
    # Clear registers for ELKS entry convention?
    xor %eax, %eax
    xor %ebx, %ebx
    xor %ecx, %ecx
    mov %ebp, %edx
    xor %ebp, %ebp
    xor %esi, %esi
    xor %edi, %edi
    
    iret

# new_user_thread_trampoline - first-run entry for a user thread created
# via SYS_THR_NEW.  Called by switch_to's `ret` on the new thread's
# initial scheduling.  Expects three cdecl-style args at the top of the
# kernel stack (pushed by sched_create_thread):
#
#   [esp+0]   user_entry   (user-mode function to invoke, e.g. __pthread_trampoline)
#   [esp+4]   user_stack   (user-mode stack top — already malloc'd in user space)
#   [esp+8]   user_arg     (single void* argument to user_entry, cdecl)
#
# Builds the iret frame on the kernel stack and IRETs to userspace.  The
# user stack is rewritten in-place to look like a normal call frame
# (return-addr 0 followed by arg) so user_entry sees `arg` at [esp+4]
# exactly as if it had been `call`'d.
# new_kernel_thread_trampoline - first-run entry for a kernel thread.
# switch_to() runs under the scheduler's intr_disable() and does not
# save/restore EFLAGS, so a freshly-scheduled kthread arrives here with
# IF=0.  Enable interrupts, then `ret` to fall through to the thread's
# entry_point (pushed just above this address by sched_create_thread).
# Without this a kthread runs with interrupts masked forever — fatal for
# any busy-poll-with-tick-timeout loop (the timer never ticks).
.global new_kernel_thread_trampoline
new_kernel_thread_trampoline:
    sti
    ret

.global new_user_thread_trampoline
new_user_thread_trampoline:
    pop %ecx                # user_entry
    pop %edx                # user_stack
    pop %eax                # user_arg

    cli

    # Load user data segments.  We can't trust whatever the previous
    # context left here.
    push %eax               # save user_arg across segment moves
    mov $0x23, %ax          # USER_DATA_SEL
    mov %ax, %ds
    mov %ax, %es
    xor %ax, %ax            # null FS — userspace can install its own selector
    mov %ax, %fs
    # %gs gets the per-thread TLS selector (GDT_TLS_START << 3 | RPL=3 = 0x33).
    # arch_switch_to has already pointed the GDT TLS slot at this thread's
    # gs_base, so loading the selector here picks up the right descriptor.
    # Without this, %gs starts as 0 and the first __thread access in user
    # mode reads from %gs:0 = address 0 → SIGSEGV.
    mov $0x33, %ax          # (GDT_TLS_START=6) << 3 | DPL=3
    mov %ax, %gs
    pop %eax                # restore user_arg

    # Build the cdecl call frame on the *user* stack so the trampoline
    # sees `arg` at [esp+4] like a normal call.
    sub $4, %edx
    movl %eax, (%edx)       # *user_stack = arg
    sub $4, %edx
    movl $0, (%edx)         # *user_stack = return-addr 0
                            # (returning would _exit; trampoline does that
                            #  itself via pthread_exit)

    # Build IRET frame: push in order [SS, ESP, EFLAGS, CS, EIP]
    push $0x23              # User SS
    push %edx               # User ESP
    pushf
    pop %eax
    or $0x200, %eax         # IF=1 (enable interrupts in user mode)
    push %eax               # EFLAGS
    push $0x1B              # User CS
    push %ecx               # User EIP

    # Clear GPRs so the new thread starts with a clean slate.
    xor %eax, %eax
    xor %ebx, %ebx
    xor %ecx, %ecx
    xor %edx, %edx
    xor %esi, %esi
    xor %edi, %edi
    xor %ebp, %ebp

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
