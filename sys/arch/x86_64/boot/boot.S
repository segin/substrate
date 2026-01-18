/*
 * boot.S - x86_64 Long Mode Bootstrap
 *
 * This code is initially executed in 32-bit protected mode (from Multiboot2
 * or UEFI) and transitions to 64-bit Long Mode before jumping to kmain.
 *
 * Memory Map (after bootstrap):
 *   0x0000000000000000 - 0x00007FFFFFFFFFFF : User space (128 TB)
 *   0xFFFF800000000000 - 0xFFFF8FFFFFFFFFFF : Direct physical mapping
 *   0xFFFFFFFF80000000 - 0xFFFFFFFFFFFFFFFF : Kernel (-2GB)
 */

#define ASM_FILE 1

/* Multiboot2 constants */
#define MB2_MAGIC       0xE85250D6
#define MB2_ARCH_I386   0
#define MB2_HEADER_LEN  (mb2_header_end - mb2_header)
#define MB2_CHECKSUM    -(MB2_MAGIC + MB2_ARCH_I386 + MB2_HEADER_LEN)

/* Page table entry flags */
#define PTE_P           0x001   /* Present */
#define PTE_W           0x002   /* Writable */
#define PTE_PS          0x080   /* Page Size (2MB/1GB) */
#define PTE_NX          0x8000000000000000  /* No Execute */

/* CR0 bits */
#define CR0_PE          0x00000001  /* Protected Mode Enable */
#define CR0_PG          0x80000000  /* Paging Enable */

/* CR4 bits */
#define CR4_PAE         0x00000020  /* PAE */
#define CR4_PGE         0x00000080  /* Page Global Enable */

/* MSR addresses */
#define MSR_EFER        0xC0000080
#define EFER_LME        0x100       /* Long Mode Enable */
#define EFER_NXE        0x800       /* No Execute Enable */

/* Kernel virtual base */
#define KERNEL_VMA      0xFFFFFFFF80000000

.code32

/*
 * Multiboot2 Header
 */
.section .multiboot2, "a"
.align 8
mb2_header:
    .long MB2_MAGIC
    .long MB2_ARCH_I386
    .long MB2_HEADER_LEN
    .long MB2_CHECKSUM
    
    /* Framebuffer tag */
    .align 8
    .short 5        /* Type: framebuffer */
    .short 0        /* Flags */
    .long 20        /* Size */
    .long 1024      /* Width */
    .long 768       /* Height */
    .long 32        /* Depth */
    
    /* End tag */
    .align 8
    .short 0
    .short 0
    .long 8
mb2_header_end:

/*
 * Entry point (32-bit protected mode)
 */
.section .text.boot, "ax"
.global _start
.type _start, @function
_start:
    cli
    cld
    
    /* Save Multiboot2 info pointer (EBX) and magic (EAX) */
    movl %eax, %esi
    movl %ebx, %edi

    /* Check for Long Mode support */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb no_long_mode
    
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx      /* LM bit */
    jz no_long_mode

    /*
     * Setup 4-level page tables for Long Mode
     * Using 2MB pages (PS bit) for simplicity during bootstrap
     *
     * PML4[0] -> PDPT (identity, low 512GB)
     * PML4[511] -> PDPT (kernel, high 512GB)
     * PDPT[0] -> PD (first 1GB)
     * PDPT[510] -> PD (for kernel at -2GB)
     * PD entries: 2MB pages mapping first 1GB
     */
    
    /* Clear page tables */
    movl $(boot_pml4 - KERNEL_VMA), %edi
    xorl %eax, %eax
    movl $(4096 * 4 / 4), %ecx  /* 4 pages */
    rep stosl
    
    /* PML4[0] = PDPT | Present | Writable */
    movl $(boot_pml4 - KERNEL_VMA), %edi
    movl $(boot_pdpt - KERNEL_VMA), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, (%edi)
    
    /* PML4[511] = PDPT | Present | Writable (for -2GB kernel) */
    movl %eax, 511*8(%edi)
    
    /* PDPT[0] = PD | Present | Writable (identity map first 1GB) */
    movl $(boot_pdpt - KERNEL_VMA), %edi
    movl $(boot_pd - KERNEL_VMA), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, (%edi)
    
    /* PDPT[510] = PD | Present | Writable (kernel at -2GB) */
    movl %eax, 510*8(%edi)
    
    /* Fill PD with 2MB pages (512 entries = 1GB) */
    movl $(boot_pd - KERNEL_VMA), %edi
    movl $(PTE_P | PTE_W | PTE_PS), %eax  /* 2MB page, P, W */
    xorl %ebx, %ebx                        /* Physical address counter */
    movl $512, %ecx
1:
    movl %eax, (%edi)
    movl %ebx, 4(%edi)                     /* High 32 bits of address (0 for <4GB) */
    addl $(2 * 1024 * 1024), %eax          /* Next 2MB */
    adcl $0, %ebx
    addl $8, %edi
    decl %ecx
    jnz 1b

    /*
     * Enable Long Mode
     */
    
    /* Load CR3 with PML4 physical address */
    movl $(boot_pml4 - KERNEL_VMA), %eax
    movl %eax, %cr3
    
    /* Enable PAE in CR4 */
    movl %cr4, %eax
    orl $(CR4_PAE | CR4_PGE), %eax
    movl %eax, %cr4
    
    /* Enable Long Mode in EFER MSR */
    movl $MSR_EFER, %ecx
    rdmsr
    orl $(EFER_LME | EFER_NXE), %eax
    wrmsr
    
    /* Enable paging in CR0 (activates Long Mode) */
    movl %cr0, %eax
    orl $(CR0_PE | CR0_PG), %eax
    movl %eax, %cr0

    /*
     * Load 64-bit GDT and jump to 64-bit code
     */
    lgdt (boot_gdt_ptr - KERNEL_VMA)
    
    /* Far jump to 64-bit code segment */
    ljmp $0x08, $(long_mode_entry - KERNEL_VMA)

no_long_mode:
    /* Display error and halt */
    movl $0xB8000, %edi
    movl $0x4F454F4E, (%edi)          /* "NE" red on white */
    movl $0x4F4C4F4F, 4(%edi)         /* "OL" */
    movl $0x4F204F4D, 8(%edi)         /* "M " */
1:  hlt
    jmp 1b

/*
 * 64-bit Long Mode entry point
 */
.code64
.section .text.boot64, "ax"
long_mode_entry:
    /* Load 64-bit data segments */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    
    /* Jump to higher half using absolute address */
    movabsq $higher_half, %rax
    jmp *%rax

.section .text
higher_half:
    /* Setup kernel stack */
    movabsq $stack_top, %rsp
    
    /* Clear identity mapping (PML4[0]) */
    movabsq $boot_pml4, %rdi
    movq $0, (%rdi)
    
    /* Flush TLB */
    movq %cr3, %rax
    movq %rax, %cr3
    
    /* Clear RFLAGS */
    pushq $0
    popfq
    
    /* Convert Multiboot info pointer to virtual address */
    movl %edi, %edi             /* Zero-extend */
    addq $KERNEL_VMA, %rdi      /* Physical + KERNEL_VMA */
    
    /* Zero-extend magic number */
    movl %esi, %esi
    
    /* Call kmain(magic, info) - System V AMD64 ABI */
    movq %rsi, %rdi             /* Arg1: magic */
    /* RDI already has info pointer */
    xchgq %rdi, %rsi            /* Swap: RDI=magic, RSI=info */
    
    call kmain

    /* Shouldn't return, but halt if it does */
    cli
1:  hlt
    jmp 1b

/*
 * Bootstrap GDT (minimal for Long Mode entry)
 */
.section .rodata.boot
.align 16
boot_gdt:
    .quad 0x0000000000000000    /* Null descriptor */
    .quad 0x00AF9A000000FFFF    /* 64-bit code: DPL=0, L=1, D=0 */
    .quad 0x00AF92000000FFFF    /* 64-bit data: DPL=0 */
boot_gdt_end:

boot_gdt_ptr:
    .word boot_gdt_end - boot_gdt - 1
    .quad boot_gdt - KERNEL_VMA

/*
 * Bootstrap page tables (4KB aligned)
 */
.section .bss.boot
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt:
    .skip 4096
boot_pd:
    .skip 4096

/*
 * Kernel stack
 */
.section .bss
.align 16
stack_bottom:
    .skip 32768                 /* 32KB stack */
stack_top:
