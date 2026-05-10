/*
 * ld.h — internal types for /sbin/ld.so.
 *
 * Phase 2 surface: enough of the ELF / auxv types to walk the
 * program's PT_DYNAMIC.  We deliberately do NOT include any
 * Substrate libc headers; the linker is freestanding.
 */

#ifndef _LD_SO_LD_H
#define _LD_SO_LD_H

typedef unsigned int   ld_u32;
typedef int            ld_i32;
typedef unsigned short ld_u16;
typedef unsigned long  ld_size;

/* ELF32 program header — System V ELF spec Fig. 2-1 */
typedef struct {
    ld_u32 p_type;
    ld_u32 p_offset;
    ld_u32 p_vaddr;
    ld_u32 p_paddr;
    ld_u32 p_filesz;
    ld_u32 p_memsz;
    ld_u32 p_flags;
    ld_u32 p_align;
} Elf32_Phdr;

/* ELF32 dynamic entry */
typedef struct {
    ld_i32 d_tag;
    union { ld_u32 d_val; ld_u32 d_ptr; } d_un;
} Elf32_Dyn;

/* ELF32 REL relocation */
typedef struct {
    ld_u32 r_offset;
    ld_u32 r_info;
} Elf32_Rel;

#define ELF32_R_TYPE(i) ((i) & 0xff)
#define ELF32_R_SYM(i)  ((i) >> 8)

/* Dynamic tags we use in Phase 2.  Full list lands in Phase 3. */
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_STRSZ   10
#define DT_SYMENT  11
#define DT_REL     17
#define DT_RELSZ   18
#define DT_RELENT  19
#define DT_PLTREL  20
#define DT_DEBUG   21
#define DT_TEXTREL 22
#define DT_JMPREL  23
#define DT_GNU_HASH 0x6ffffef5
#define DT_PLTRELSZ 2
#define DT_PLTGOT   3

/* Program-header types */
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_PHDR     6
#define PT_TLS      7

/* i386 relocation types — only R_386_RELATIVE used by self-reloc */
#define R_386_NONE     0
#define R_386_32       1
#define R_386_PC32     2
#define R_386_GOT32    3
#define R_386_PLT32    4
#define R_386_COPY     5
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8

/* Auxv entries used in Phase 2 */
#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_FLAGS   8
#define AT_ENTRY   9
#define AT_PLATFORM 15
#define AT_EXECFN  31

/* Native syscall numbers we actually issue from the linker. */
#define SYS_write 4
#define SYS_exit  1

/* Tiny IO helpers — implemented in ld_io.c. */
void  ld_write(int fd, const char *buf, ld_size len);
void  ld_puts(const char *s);
void  ld_putx(ld_u32 v);
void  ld_putd(ld_u32 v);
void  ld_die(const char *msg) __attribute__((noreturn));

/* Phase 2 entry point from asm.  Returns the program entry to
 * jump to (auxv AT_ENTRY).  Receives a pointer to the kernel's
 * initial stack (where argc lives). */
ld_u32 ld_main(ld_u32 *initial_stack);

#endif /* _LD_SO_LD_H */
