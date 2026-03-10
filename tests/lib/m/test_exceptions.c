/*
 * test_exceptions.c - Test floating-point exceptions directly
 */

#include <stdio.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

int main() {
    int exceptions;
    uint16_t sw, cw;
    
    printf("Testing exception raising and detection\n");
    
    /* Read initial state directly */
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    printf("Initial status word: 0x%04x, control word: 0x%04x\n", sw, cw);
    
    /* Direct status word manipulation via fnstenv/fldenv */
    fenv_t env;
    __asm__ __volatile__("fnstenv %0" : "=m"(env));
    env.__status_word |= FE_INEXACT;
    __asm__ __volatile__("fldenv %0" : : "m"(env));
    
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
    printf("After direct set via fldenv: 0x%04x\n", sw);
    
    return 0;
}
