/*
 * test_control_word.c - Test to inspect x87 control and status words
 */

#include <stdio.h>
#include <stdint.h>

int main() {
    uint16_t cw, sw;
    
    /* Read control word */
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    printf("Control word (CW): 0x%04x\n", cw);
    printf("  Exception mask: 0x%02x\n", cw & 0x3F);
    printf("  Rounding mode: 0x%x\n", (cw >> 10) & 0x03);
    
    /* Read status word */
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    printf("Status word (SW): 0x%04x\n", sw);
    printf("  Exception flags: 0x%02x\n", sw & 0x3F);
    
    /* Try to unmask exceptions */
    printf("\nUnmasking all exceptions...\n");
    cw &= ~0x3F;
    __asm__ __volatile__("fldcw %0" : : "m"(cw));
    
    /* Verify changes */
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    printf("Control word after unmask: 0x%04x\n", cw);
    printf("  Exception mask: 0x%02x\n", cw & 0x3F);
    
    /* Perform an inexact operation */
    volatile float x = 1.0f / 3.0f;
    (void)x;
    
    /* Check status word */
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    printf("Status word after 1.0f/3.0f: 0x%04x\n", sw);
    printf("  Exception flags: 0x%02x\n", sw & 0x3F);
    
    return 0;
}
