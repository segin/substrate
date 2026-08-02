#ifndef _PANIC_H
#define _PANIC_H

#include <stddef.h>
#include <stdint.h>

struct registers;

void panic(const char *msg);
void panic_with_regs(const char *msg, const struct registers *regs);

/*
 * Is every byte of [va, va+len) backed by a present page in the CURRENT
 * address space?  Walks the hardware page tables straight off CR3, so it is
 * usable from a fault handler where kernel state may already be untrustworthy.
 * Callers in the panic/stack-dump path use it so that dumping a corrupted
 * pointer reports "<unsafe to read>" instead of taking a second fault inside
 * the handler for the first one.
 */
int panic_addr_readable(uintptr_t va, size_t len);

#endif
