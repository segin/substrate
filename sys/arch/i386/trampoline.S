.code16
.global trampoline_start
.global trampoline_end
.global trampoline_cr3
.global trampoline_cr4
.global trampoline_cr0
.global trampoline_stack
.global trampoline_entry

#define TRAMPOLINE_BASE 0x8000
#define TRAMPOLINE_PHYS(sym) (TRAMPOLINE_BASE + (sym - trampoline_start))

trampoline_start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    
    # Load temporary GDT from the copied low-memory image.
    lgdt TRAMPOLINE_PHYS(trampoline_gdt_ptr)

    # Enable protected mode
    mov %cr0, %eax
    or $1, %al
    mov %eax, %cr0

    # Far jump to the copied 32-bit entry in low memory.
    ljmp $0x08, $TRAMPOLINE_PHYS(trampoline_start32)

.code32
trampoline_start32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    # Mirror the BSP paging/control-register setup before entering the kernel.
    mov TRAMPOLINE_PHYS(trampoline_cr4), %eax
    mov %eax, %cr4
    mov TRAMPOLINE_PHYS(trampoline_cr3), %eax
    mov %eax, %cr3
    mov TRAMPOLINE_PHYS(trampoline_cr0), %eax
    mov %eax, %cr0
    jmp 1f
1:
    # Paging is active; switch to the AP's higher-half kernel stack.
    mov TRAMPOLINE_PHYS(trampoline_stack), %esp

    # Call AP entry point in the higher half.
    call *TRAMPOLINE_PHYS(trampoline_entry)

    # Hang if return
2:  hlt
    jmp 2b

.align 4
trampoline_gdt:
    .long 0, 0
    .long 0x0000FFFF, 0x00CF9A00 # Code
    .long 0x0000FFFF, 0x00CF9200 # Data
trampoline_gdt_end:

trampoline_gdt_ptr:
    .short (trampoline_gdt_end - trampoline_gdt - 1)
    .long TRAMPOLINE_PHYS(trampoline_gdt)

.align 4
trampoline_cr3: .long 0
trampoline_cr4: .long 0
trampoline_cr0: .long 0
trampoline_stack: .long 0
trampoline_entry: .long 0

trampoline_end:
