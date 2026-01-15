#ifndef _KSYMS_H
#define _KSYMS_H

#include <stdint.h>

/* Symbol table entry */
struct ksym {
    uint32_t addr;      /* Symbol address */
    char name[56];      /* Symbol name (truncated) */
};

/*
 * ksym_init - Initialize symbol table
 *
 * Call early in boot to load/parse kernel symbols.
 */
void ksym_init(void);

/*
 * ksym_lookup - Find symbol containing address
 *
 * @addr: Kernel address to look up
 *
 * Returns pointer to symbol entry with largest address <= addr,
 * or NULL if no symbols loaded.
 */
const struct ksym *ksym_lookup(uint32_t addr);

/*
 * ksym_resolve - Resolve address to symbol string
 *
 * @addr: Address to resolve
 * @buf: Output buffer
 * @buflen: Buffer size
 *
 * Writes "function+0xoffset" or "0xaddress" if unknown.
 * Returns length written.
 */
int ksym_resolve(uint32_t addr, char *buf, int buflen);

/*
 * ksym_print - Print address with resolved symbol
 *
 * @addr: Address to print
 *
 * Outputs to console: "function+0xoffset" or hex address.
 */
void ksym_print(uint32_t addr);

#endif /* _KSYMS_H */
