#ifndef _ELF_H
#define _ELF_H

#include <stdint.h>

#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint32_t      e_entry;
    uint32_t      e_phoff;
    uint32_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define EI_OSABI 7
#define ELFOSABI_SYSV 0
#define ELFOSABI_LINUX 3
#define ELFOSABI_FREEBSD 9
#define ELFOSABI_TESTUNIX 64 // Custom ABI ID for TestUnix
#define ELFOSABI_ATT_UNIX 65 // Custom ABI ID for AT&T UNIX
#define ELFOSABI_MODESTO 11  // Novell Modesto / SVR4

#define NT_GNU_ABI_TAG 1
#define NT_GNU_HWCAP   2
#define NT_GNU_BUILD_ID 3
#define NT_GNU_GOLD_VERSION 4
#define NT_GNU_PROPERTY_TYPE_0 5

// Auxiliary Vector Types
#define AT_NULL   0
#define AT_IGNORE 1
#define AT_EXECFD 2
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_FLAGS  8
#define AT_ENTRY  9
#define AT_NOTELF 10
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_PLATFORM 15
#define AT_HWCAP  16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_EXECFN 31
#define AT_SYSINFO 32
#define AT_SYSINFO_EHDR 33

typedef struct {
    uint32_t n_namesz;
    uint32_t n_descsz;
    uint32_t n_type;
} Elf32_Nhdr;

int elf_check_file(Elf32_Ehdr *hdr);
int elf_load_file(void *file, uint32_t size);

// New ELF loading interface
struct fs_node; // Forward declaration
uint32_t elf_load(struct fs_node *file, uint32_t load_base, char *interp_path, uint32_t *interp_len);
int elf_execve(const char *path, char *const argv[], char *const envp[]);

#endif
