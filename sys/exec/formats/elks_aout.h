#ifndef _ELKS_AOUT_H
#define _ELKS_AOUT_H

#include <stdint.h>
#include <stddef.h>

/*
 * ELKS a.out header structure (MINIX-style)
 * Based on traditional 16-bit a.out formats.
 */

struct elks_exec {
    uint8_t  a_magic[2];    /* Magic number */
    uint8_t  a_flags;       /* Header flags */
    uint8_t  a_cpu;         /* CPU type (0x01=8086, 0x02=80286) */
    uint8_t  a_hdrlen;      /* Header length */
    uint8_t  a_unused;
    uint16_t a_version;     /* Version */
    uint32_t a_text;        /* Text size */
    uint32_t a_data;        /* Data size */
    uint32_t a_bss;         /* BSS size */
    uint32_t a_entry;       /* Entry point */
    uint32_t a_total;       /* Total memory required */
    uint32_t a_syms;        /* Symbol table size */
};

/* Magic numbers */
#define ELKS_MAG0 0x01
#define ELKS_MAG1 0x03
/* Note: The task mentioned 0x0301/0x0302 as magic, which often means 
   little-endian 0x01, 0x03 or similar. */

#define ELKS_CPU_8086  1
#define ELKS_CPU_80286 2

/* Prototypes */
struct exec_binary_handler;
void elks_init_handler(void);
int elks_check_file(const char *path, const char *header, size_t len);
int elks_load(int fd, const char *path, char *const argv[], char *const envp[]);

#endif /* _ELKS_AOUT_H */
