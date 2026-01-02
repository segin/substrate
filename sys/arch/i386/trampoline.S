.code16
.global trampoline_start
.global trampoline_end

trampoline_start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    
    # Load temporary GDT
    # (Addresses are relative to trampoline start)
    lgdt (trampoline_gdt_ptr - trampoline_start)

    # Enable protected mode
    mov %cr0, %eax
    or $1, %al
    mov %eax, %cr0

    # Far jump to 32-bit mode
    # 0x08 is the code segment index
    ljmp $0x08, $(trampoline_start32 - trampoline_start)

.code32
trampoline_start32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    
    # Load stack pointer provided by BSP
    # (Located at a fixed offset)
    mov (trampoline_stack - trampoline_start), %esp
    
    # Call AP entry point
    call *(trampoline_entry - trampoline_start)

    # Hang if return
1:  hlt
    jmp 1b

.align 4
trampoline_gdt:
    .long 0, 0
    .long 0x0000FFFF, 0x00CF9A00 # Code
    .long 0x0000FFFF, 0x00CF9200 # Data

trampoline_gdt_ptr:
    .short (trampoline_gdt_ptr - trampoline_gdt - 1)
    .long trampoline_gdt

.align 4
trampoline_stack: .long 0
trampoline_entry: .long 0

trampoline_end:
