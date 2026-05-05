#ifndef _PANIC_H
#define _PANIC_H

struct registers;

void panic(const char *msg);
void panic_with_regs(const char *msg, const struct registers *regs);

#endif
