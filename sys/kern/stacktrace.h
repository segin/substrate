#ifndef _STACKTRACE_H
#define _STACKTRACE_H

#include <stdint.h>

/*
 * stack_trace - Print stack trace from current frame
 *
 * Walks EBP chain and displays up to 16 frames.
 * Safe for use in panic context.
 */
void stack_trace(void);

/*
 * stack_trace_from - Print stack trace from specified context
 *
 * @ebp: Frame pointer (EBP register value)
 * @eip: Instruction pointer at fault point
 *
 * Used by exception handlers to show stack at fault time.
 */
void stack_trace_from(uint32_t ebp, uint32_t eip);

#endif /* _STACKTRACE_H */
