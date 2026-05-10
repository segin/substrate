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
#define DT_SONAME  14
#define DT_RPATH   15
#define DT_RUNPATH 29
#define DT_INIT    12
#define DT_FINI    13
#define DT_INIT_ARRAY    25
#define DT_FINI_ARRAY    26
#define DT_INIT_ARRAYSZ  27
#define DT_FINI_ARRAYSZ  28

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
#define SYS_exit   1
#define SYS_read   3
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_lseek 19
#define SYS_mmap  90
#define SYS_fstat 108

/* mmap flags / prot bits we use. */
#define LD_PROT_READ   1
#define LD_PROT_WRITE  2
#define LD_PROT_EXEC   4
#define LD_MAP_PRIVATE 0x002
#define LD_MAP_FIXED   0x010
#define LD_MAP_ANON    0x020

/* open() flags */
#define LD_O_RDONLY 0

/* ELF32 file header — only the fields ld.so reads at load time. */
typedef struct {
    unsigned char e_ident[16];
    ld_u16 e_type;
    ld_u16 e_machine;
    ld_u32 e_version;
    ld_u32 e_entry;
    ld_u32 e_phoff;
    ld_u32 e_shoff;
    ld_u32 e_flags;
    ld_u16 e_ehsize;
    ld_u16 e_phentsize;
    ld_u16 e_phnum;
    ld_u16 e_shentsize;
    ld_u16 e_shnum;
    ld_u16 e_shstrndx;
} Elf32_Ehdr;

/* ELF32 symbol */
typedef struct {
    ld_u32 st_name;
    ld_u32 st_value;
    ld_u32 st_size;
    unsigned char st_info;
    unsigned char st_other;
    ld_u16 st_shndx;
} Elf32_Sym;

#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2
#define STN_UNDEF  0
#define SHN_UNDEF  0

/* Tiny IO helpers — implemented in ld_io.c. */
void  ld_write(int fd, const char *buf, ld_size len);
void  ld_puts(const char *s);
void  ld_putx(ld_u32 v);
void  ld_putd(ld_u32 v);
void  ld_die(const char *msg) __attribute__((noreturn));

/* Filesystem + mapping syscalls.  Return -errno on failure. */
int   ld_open(const char *path, int flags);
int   ld_close(int fd);
long  ld_read(int fd, void *buf, ld_size n);
long  ld_lseek(int fd, long off, int whence);
void *ld_mmap(void *addr, ld_size len, int prot, int flags,
              int fd, ld_u32 page_off);

/* Phase-3 surface: per-loaded-object descriptor.  Both the program
 * itself and every loaded .so get one of these.  The list is kept
 * in load order for deterministic symbol resolution. */
typedef struct ld_obj {
    char            name[64];   /* SONAME or basename, for diagnostics */
    ld_u32          base;       /* load bias */
    Elf32_Dyn      *dynamic;    /* PT_DYNAMIC pointer (already biased) */

    /* Cached dynamic-table pointers (all already biased). */
    const char     *strtab;
    Elf32_Sym      *symtab;
    ld_u32          strsz;

    ld_u32         *gnu_hash;   /* DT_GNU_HASH (preferred) */
    ld_u32         *hash;       /* DT_HASH (fallback) */

    Elf32_Rel      *rel;        /* DT_REL */
    ld_u32          relsz;      /* bytes */
    Elf32_Rel      *jmprel;     /* DT_JMPREL */
    ld_u32          pltrelsz;   /* bytes */

    struct ld_obj  *next;
} ld_obj_t;

/* Load a shared object by absolute path.  Returns NULL on failure
 * with a diagnostic via ld_die.  Already-loaded SONAMEs are
 * deduplicated; the cached descriptor is returned. */
ld_obj_t *ld_load_object(const char *path);

/* Walk the loaded-object list looking for `name`.  Returns the
 * symbol's runtime address, or 0 if undefined / weak-undef. */
ld_u32 ld_resolve(const char *name);

/* Apply DT_REL and DT_JMPREL on `obj`.  Returns 0 on success. */
int ld_relocate(ld_obj_t *obj);

/* Public head of the loaded-object list. */
ld_obj_t *ld_obj_list(void);

/* Phase 2 entry point from asm. */
ld_u32 ld_main(ld_u32 *initial_stack);

#endif /* _LD_SO_LD_H */
