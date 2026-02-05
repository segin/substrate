/*
 * sys/arch/x86_64/pmap_asm.S
 *
 * MSR helpers for x86_64 pmap.
 */

#define _ASM_FILE_

    .text
    .global pmap_rdmsr
    .global pmap_wrmsr

/*
 * uint64_t pmap_rdmsr(uint32_t msr)
 *
 * RDI: msr index
 * Returns: 64-bit value in RAX
 */
pmap_rdmsr:
    movl    %edi, %ecx      /* MSR index in ECX */
    rdmsr                   /* Read MSR into EDX:EAX */
    shlq    $32, %rdx       /* Shift high part */
    orq     %rdx, %rax      /* Combine into RAX */
    ret

/*
 * void pmap_wrmsr(uint32_t msr, uint64_t val)
 *
 * RDI: msr index
 * RSI: value to write
 */
pmap_wrmsr:
    movl    %edi, %ecx      /* MSR index in ECX */
    movq    %rsi, %rax      /* Low 32 bits in EAX (implicit) */
    movq    %rsi, %rdx      /* Copy value to RDX */
    shrq    $32, %rdx       /* High 32 bits in EDX */
    wrmsr                   /* Write EDX:EAX into MSR[ECX] */
    ret

    .section .note.GNU-stack,"",@progbits
