#ifndef _E820_H
#define _E820_H

#include <stdint.h>

#define E820_USABLE     1
#define E820_RESERVED   2
#define E820_ACPI       3
#define E820_NVS        4
#define E820_BAD        5

typedef struct e820_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) e820_entry_t;

#endif
