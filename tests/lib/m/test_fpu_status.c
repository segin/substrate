/*
 * test_fpu_status.c - Test x87 FPU status word behavior
 */

#include <stdio.h>
#include <stdint.h>

int main() {
    uint16_t sw, cw;
    
    /* Read initial state */
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    printf("Initial status word: 0x%04x, control word: 0x%04x\n", sw, cw);
    
    /* Try to set bits in status word */
    printf("\n=== Testing direct status word manipulation ===\n");
    
    /* Test: using fnstenv/fldenv with proper env structure */
    char env[28];  // x87 environment is 28 bytes
    
    __asm__ __volatile__("fnstenv %0" : "=m"(env));
    uint16_t *status_word_ptr = (uint16_t*)(env + 2);
    printf("env before modification: SW=0x%04x\n", *status_word_ptr);
    *status_word_ptr |= 0x0020;  // Set FE_INEXACT
    
    __asm__ __volatile__("fldenv %0" : : "m"(env));
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    printf("After fldenv with SW=0x%04x: status word = 0x%04x\n", *status_word_ptr, sw);
    
    /* Test 2: using FCLEX to clear */
    printf("\n=== Testing FCLEX ===\n");
    __asm__ __volatile__("fnclex");
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    printf("After fnclex: status word = 0x%04x\n", sw);
    
    /* Test 3: performing an actual floating-point operation */
    printf("\n=== Testing actual FP operation ===\n");
    
    // Clear exceptions first
    __asm__ __volatile__("fnclex");
    
    // Perform 1.0 / 3.0 which should set FE_INEXACT
    double x, y, result;
    x = 1.0;
    y = 3.0;
    
    __asm__ __volatile__(
        "fldl %1\n\t"
        "fldl %2\n\t"
        "fdivp\n\t"
        "fstpl %0"
        : "=m"(result)
        : "m"(x), "m"(y)
        : "cc"
    );
    
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    printf("1.0 / 3.0 = %f\n", result);
    printf("Status word: 0x%04x\n", sw);
    printf("Exception bits: 0x%02x\n", sw & 0x3F);
    
    return 0;
}
