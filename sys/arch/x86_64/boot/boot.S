/*
 * boot.S - x86_64 kernel entry: 32-bit protected mode to the higher half
 *
 * The loader (GRUB with multiboot or multiboot2, or QEMU's -kernel, which
 * speaks multiboot 1) enters _start in 32-bit protected mode with paging
 * off, at the kernel's physical load address.  This code builds the boot
 * page tables, enables PAE and long mode, enters 64-bit mode, jumps to the
 * kernel's link address in the top 2 GiB and calls kmain64(magic, info).
 *
 * Boot page tables (2 MiB pages; replaced once the 4-level pmap exists):
 *
 *   0x0000000000000000 + 1 GiB  identity, needed only across the jump to
 *                               the higher half; dropped in kmain64()
 *   KERNEL_VMA         + 1 GiB  the kernel image (phys 0 .. 1 GiB)
 *   DMAP_BASE          + 4 GiB  direct map of the first 4 GiB of physical
 *                               memory (MMIO and the loader's structures)
 *
 * The 32-bit code runs at its physical address while linked at its
 * virtual one, so every absolute reference in it is written sym - KERNEL_VMA.
 */

#define ASM_FILE 1
#include <arch/x86-common/multiboot.h>
#include <arch/x86_64/layout.h>

#define MB1_MAGIC       0x1BADB002
#define MB1_FLAGS       0x00010007  /* page-align, memory map, video mode,
                                       a.out kludge */

#define PTE_P           0x001
#define PTE_W           0x002
#define PTE_PS          0x080

#define CR0_PG          0x80000000
#define CR0_WP          0x00010000
#define CR4_PAE         0x00000020
#define CR4_PGE         0x00000080
#define MSR_EFER        0xC0000080
#define EFER_LME        0x00000100
#define EFER_NXE        0x00000800

#define PHYS(sym)       ((sym) - KERNEL_VMA)
#define PML4_SLOT(va)   (((va) >> 39) & 511)
#define PDPT_SLOT(va)   (((va) >> 30) & 511)

/*
 * Multiboot 1 header.  The a.out-kludge fields describe the flat image
 * (kernel-x86_64.bin) by physical address, which is what lets QEMU's
 * -kernel and GRUB's `multiboot` load a 64-bit kernel at all: both refuse
 * an ELF64 file otherwise.
 */
.section .multiboot, "a"
.align 4
mb1_header:
    .long MB1_MAGIC
    .long MB1_FLAGS
    .long -(MB1_MAGIC + MB1_FLAGS)
    .long PHYS(mb1_header)          /* header_addr */
    .long PHYS(_kernel_start)       /* load_addr */
    .long PHYS(_load_end)           /* load_end_addr */
    .long PHYS(_bss_end)            /* bss_end_addr */
    .long PHYS(_start)              /* entry_addr */
    /* The video request of the i386 /vmunix (kernel.fb.bin): a linear
     * framebuffer, which the loader sets up and reports in the
     * information structure for the framebuffer console and X. */
    .long 0                         /* mode_type: linear framebuffer */
    .long 1280                      /* width */
    .long 800                       /* height */
    .long 32                        /* depth */

/*
 * Multiboot 2 header: GRUB's `multiboot2` loads the ELF64 file (installed
 * as /vmunix64) directly, by its segments' physical addresses.
 */
.section .multiboot2, "a"
.align 8
mb2_header:
    .long MULTIBOOT2_HEADER_MAGIC
    .long MULTIBOOT2_ARCH_I386      /* entered in 32-bit protected mode */
    .long mb2_header_end - mb2_header
    .long -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCH_I386 + (mb2_header_end - mb2_header))
    /* The ELF entry point is _start's higher-half link address, which the
     * loader cannot jump to with paging off; name the physical one. */
    .align 8
    .short MULTIBOOT2_HEADER_TAG_ENTRY_ADDRESS
    .short 0
    .long 12
    .long PHYS(_start)
    /* The information tags the kernel reads, as the i386 kernel asks. */
    .align 8
mb2_inforeq:
    .short MULTIBOOT2_HEADER_TAG_INFO_REQUEST
    .short 0
    .long mb2_inforeq_end - mb2_inforeq
    .long MULTIBOOT2_TAG_TYPE_CMDLINE
    .long MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO
    .long MULTIBOOT2_TAG_TYPE_MMAP
    .long MULTIBOOT2_TAG_TYPE_FRAMEBUFFER
    .long MULTIBOOT2_TAG_TYPE_ACPI_OLD
    .long MULTIBOOT2_TAG_TYPE_ACPI_NEW
mb2_inforeq_end:
    /* The same framebuffer request as the multiboot 1 header; optional, so
     * a loader that cannot set a graphics mode still boots the kernel. */
    .align 8
    .short MULTIBOOT2_HEADER_TAG_FRAMEBUFFER
    .short 1                        /* optional */
    .long 20
    .long 1280                      /* width */
    .long 800                       /* height */
    .long 32                        /* depth */
    .align 8
    .short MULTIBOOT2_HEADER_TAG_END
    .short 0
    .long 8
mb2_header_end:

.section .text.boot, "ax"
.code32
.global _start
.type _start, @function
_start:
    cli
    cld
    movl $PHYS(boot_stack_top), %esp
    /* Loader magic and the multiboot information's physical address, kept
     * on the stack: every general register is needed for the setup. */
    pushl %eax
    pushl %ebx

    /* Long mode? */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb no_long_mode
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx
    jz no_long_mode
    movl %edx, %ebp                 /* remember NX support (bit 20) */

    /* The page tables live in .bss, which the loader has zeroed. */

    /* PML4: identity, direct map, kernel. */
    movl $PHYS(boot_pdpt_lo), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, PHYS(boot_pml4) + 8 * 0
    movl $PHYS(boot_pdpt_dmap), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, PHYS(boot_pml4) + 8 * PML4_SLOT(DMAP_BASE)
    movl $PHYS(boot_pdpt_hi), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, PHYS(boot_pml4) + 8 * PML4_SLOT(KERNEL_VMA)

    /* Identity and kernel windows share one directory: phys 0 .. 1 GiB. */
    movl $PHYS(boot_pd), %eax
    orl $(PTE_P | PTE_W), %eax
    movl %eax, PHYS(boot_pdpt_lo)
    movl %eax, PHYS(boot_pdpt_hi) + 8 * PDPT_SLOT(KERNEL_VMA)

    movl $PHYS(boot_pd), %edi
    movl $(PTE_P | PTE_W | PTE_PS), %eax
    movl $512, %ecx
1:  movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop 1b

    /* Direct map: four directories, phys 0 .. 4 GiB. */
    movl $PHYS(boot_pdpt_dmap), %edi
    movl $PHYS(boot_pd_dmap), %eax
    orl $(PTE_P | PTE_W), %eax
    movl $4, %ecx
2:  movl %eax, (%edi)
    addl $4096, %eax
    addl $8, %edi
    loop 2b

    movl $PHYS(boot_pd_dmap), %edi
    movl $(PTE_P | PTE_W | PTE_PS), %eax
    xorl %edx, %edx                 /* bits 63:32 of the physical address */
    movl $2048, %ecx
3:  movl %eax, (%edi)
    movl %edx, 4(%edi)
    addl $0x200000, %eax
    adcl $0, %edx
    addl $8, %edi
    loop 3b

    /* Enter long mode. */
    movl $PHYS(boot_pml4), %eax
    movl %eax, %cr3
    movl %cr4, %eax
    orl $(CR4_PAE | CR4_PGE), %eax
    movl %eax, %cr4
    movl $MSR_EFER, %ecx
    rdmsr
    orl $EFER_LME, %eax
    testl $(1 << 20), %ebp
    jz 4f
    orl $EFER_NXE, %eax
4:  wrmsr
    movl %cr0, %eax
    orl $(CR0_PG | CR0_WP), %eax
    movl %eax, %cr0

    popl %esi                       /* information */
    popl %edi                       /* magic */
    lgdt PHYS(boot_gdt_ptr)
    ljmp $0x08, $PHYS(long_mode_entry)

no_long_mode:
    /* No serial yet: say it on the VGA text screen. */
    movl $0xB8000, %edi
    movl $0x4F6F4F4E, (%edi)        /* "No" */
    movl $0x4F364F20, 4(%edi)       /* " 6" */
    movl $0x4F2D4F34, 8(%edi)       /* "4-" */
    movl $0x4F694F62, 12(%edi)      /* "bi" */
    movl $0x4F204F74, 16(%edi)      /* "t " */
5:  hlt
    jmp 5b

.code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    movabsq $higher_half, %rax
    jmp *%rax

.section .text
higher_half:
    movabsq $boot_stack_top, %rsp
    xorl %ebp, %ebp
    pushq $0
    popfq
    /* kmain64(magic, info): %edi and %esi, loaded by 32-bit pops, so the
     * upper halves are zero. */
    call kmain64
6:  cli
    hlt
    jmp 6b

.section .rodata
.align 16
boot_gdt:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF        /* 0x08: kernel code, 64-bit */
    .quad 0x00CF92000000FFFF        /* 0x10: kernel data */
boot_gdt_end:
boot_gdt_ptr:
    .word boot_gdt_end - boot_gdt - 1
    .long PHYS(boot_gdt)

.section .bss.boot, "aw", @nobits
.align 4096
.global boot_pml4
boot_pml4:      .skip 4096
boot_pdpt_lo:   .skip 4096
boot_pdpt_hi:   .skip 4096
boot_pdpt_dmap: .skip 4096
boot_pd:        .skip 4096
boot_pd_dmap:   .skip 4096 * 4

.section .bss
.align 16
.global boot_stack_top
boot_stack:     .skip 16384
boot_stack_top:
