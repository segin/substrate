#ifndef _ELF_H
#define _ELF_H

#include <stdint.h>

/* ELF identification index */
#define EI_MAG0         0   /* File identification */
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4   /* File class */
#define EI_DATA         5   /* Data encoding */
#define EI_VERSION      6   /* File version */
#define EI_OSABI        7   /* OS/ABI identification */
#define EI_ABIVERSION   8   /* ABI version */
#define EI_PAD          9   /* Start of padding bytes */
#define EI_NIDENT       16  /* Size of e_ident[] */

/* Magic bytes */
#define ELFMAG0         0x7f
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'
#define ELFMAG          "\177ELF"
#define SELFMAG         4

/* ELF Class */
#define ELFCLASSNONE    0
#define ELFCLASS32      1
#define ELFCLASS64      2

/* Data encoding */
#define ELFDATANONE     0
#define ELFDATA2LSB     1
#define ELFDATA2MSB     2

/* ELF version */
#define EV_NONE         0
#define EV_CURRENT      1

/* OS/ABI */
#define ELFOSABI_NONE       0
#define ELFOSABI_SYSV       0
#define ELFOSABI_HPUX       1
#define ELFOSABI_NETBSD     2
#define ELFOSABI_LINUX      3
#define ELFOSABI_GNU        3
#define ELFOSABI_SOLARIS    6
#define ELFOSABI_AIX        7
#define ELFOSABI_IRIX       8
#define ELFOSABI_FREEBSD    9
#define ELFOSABI_TRU64      10
#define ELFOSABI_MODESTO    11
#define ELFOSABI_OPENBSD    12
#define ELFOSABI_OPENVMS    13
#define ELFOSABI_NSK        14
#define ELFOSABI_AROS       15
#define ELFOSABI_ARM        97
#define ELFOSABI_STANDALONE 255
#define ELFOSABI_SUBSTRATE  64  /* Substrate OS */

/* Object file types */
#ifndef ET_NONE
#define ET_NONE     0
#define ET_REL      1
#define ET_EXEC     2
#define ET_DYN      3
#define ET_CORE     4
#define ET_LOOS     0xfe00
#define ET_HIOS     0xfeff
#define ET_LOPROC   0xff00
#define ET_HIPROC   0xffff
#endif

/* Machine types */
#ifndef EM_386
#define EM_NONE     0
#define EM_386      3
#define EM_68K      4
#define EM_MIPS     8
#define EM_PPC      20
#define EM_PPC64    21
#define EM_ARM      40
#define EM_X86_64   62
#define EM_AARCH64  183
#define EM_RISCV    243
#endif

/* Section types */
#ifndef SHT_NULL
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_HASH        5
#define SHT_DYNAMIC     6
#define SHT_NOTE        7
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHT_SHLIB       10
#define SHT_DYNSYM      11
#define SHT_INIT_ARRAY  14
#define SHT_FINI_ARRAY  15
#define SHT_PREINIT_ARRAY 16
#define SHT_GROUP       17
#define SHT_SYMTAB_SHNDX 18
#define SHT_GNU_HASH    0x6ffffff6
#define SHT_GNU_ATTRIBUTES 0x6ffffff5
#define SHT_LOOS        0x60000000
#define SHT_HIOS        0x6fffffff
#define SHT_LOPROC      0x70000000
#define SHT_HIPROC      0x7fffffff
#define SHT_LOUSER      0x80000000
#define SHT_HIUSER      0xffffffff
#endif

/* ARM-specific */
#define SHT_ARM_EXIDX       0x70000001
#define SHT_ARM_PREEMPTMAP  0x70000002
#define SHT_ARM_ATTRIBUTES  0x70000003
#define SHT_AARCH64_ATTRIBUTES 0x70000003

/* Section flags */
#define SHF_WRITE       0x1
#define SHF_ALLOC       0x2
#define SHF_EXECINSTR   0x4
#define SHF_MERGE       0x10
#define SHF_STRINGS     0x20
#define SHF_INFO_LINK   0x40
#define SHF_LINK_ORDER  0x80
#define SHF_OS_NONCONFORMING 0x100
#define SHF_GROUP       0x200
#define SHF_TLS         0x400
#define SHF_COMPRESSED  0x800
#define SHF_EXCLUDE     0x80000000
#define SHF_MASKOS      0x0ff00000
#define SHF_MASKPROC    0xf0000000
#define SHF_ARM_PURECODE 0x20000000

/* Special section numbers */
#define SHN_UNDEF   0
#define SHN_LORESERVE 0xff00
#define SHN_LOPROC  0xff00
#define SHN_HIPROC  0xff1f
#define SHN_LOOS    0xff20
#define SHN_HIOS    0xff3f
#define SHN_ABS     0xfff1
#define SHN_COMMON  0xfff2
#define SHN_XINDEX  0xffff
#define SHN_HIRESERVE 0xffff

/* Segment types */
#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6
#define PT_TLS          7
#define PT_LOOS         0x60000000
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552
#define PT_GNU_PROPERTY 0x6474e553
#define PT_HIOS         0x6fffffff
#define PT_LOPROC       0x70000000
#define PT_HIPROC       0x7fffffff
#define PT_ARM_EXIDX    0x70000001
#define PT_AARCH64_MEMTAG_MTE 0x70000002

/* Segment flags */
#define PF_X    0x1
#define PF_W    0x2
#define PF_R    0x4

/* Symbol binding */
#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2
#define STB_GNU_UNIQUE 10

/* Symbol type */
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_COMMON  5
#define STT_TLS     6
#define STT_GNU_IFUNC 10

/* Basic ELF types */
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;
typedef uint64_t Elf32_Xword;

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

/* ELF32 header */
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half  e_type;
    Elf32_Half  e_machine;
    Elf32_Word  e_version;
    Elf32_Addr  e_entry;
    Elf32_Off   e_phoff;
    Elf32_Off   e_shoff;
    Elf32_Word  e_flags;
    Elf32_Half  e_ehsize;
    Elf32_Half  e_phentsize;
    Elf32_Half  e_phnum;
    Elf32_Half  e_shentsize;
    Elf32_Half  e_shnum;
    Elf32_Half  e_shstrndx;
} Elf32_Ehdr;

/* ELF64 header */
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;
    Elf64_Off   e_phoff;
    Elf64_Off   e_shoff;
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;
    Elf64_Half  e_phentsize;
    Elf64_Half  e_phnum;
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
} Elf64_Ehdr;

/* ELF32 section header */
typedef struct {
    Elf32_Word  sh_name;
    Elf32_Word  sh_type;
    Elf32_Word  sh_flags;
    Elf32_Addr  sh_addr;
    Elf32_Off   sh_offset;
    Elf32_Word  sh_size;
    Elf32_Word  sh_link;
    Elf32_Word  sh_info;
    Elf32_Word  sh_addralign;
    Elf32_Word  sh_entsize;
} Elf32_Shdr;

/* ELF64 section header */
typedef struct {
    Elf64_Word  sh_name;
    Elf64_Word  sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr  sh_addr;
    Elf64_Off   sh_offset;
    Elf64_Xword sh_size;
    Elf64_Word  sh_link;
    Elf64_Word  sh_info;
    Elf64_Xword sh_addralign;
    Elf64_Xword sh_entsize;
} Elf64_Shdr;

/* ELF32 program header */
typedef struct {
    Elf32_Word  p_type;
    Elf32_Off   p_offset;
    Elf32_Addr  p_vaddr;
    Elf32_Addr  p_paddr;
    Elf32_Word  p_filesz;
    Elf32_Word  p_memsz;
    Elf32_Word  p_flags;
    Elf32_Word  p_align;
} Elf32_Phdr;

/* ELF64 program header */
typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

/* ELF32 symbol */
typedef struct {
    Elf32_Word  st_name;
    Elf32_Addr  st_value;
    Elf32_Word  st_size;
    unsigned char st_info;
    unsigned char st_other;
    Elf32_Half  st_shndx;
} Elf32_Sym;

/* ELF64 symbol */
typedef struct {
    Elf64_Word  st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf64_Half  st_shndx;
    Elf64_Addr  st_value;
    Elf64_Xword st_size;
} Elf64_Sym;

#define ELF32_ST_BIND(i)   ((i) >> 4)
#define ELF32_ST_TYPE(i)   ((i) & 0xf)
#define ELF32_ST_INFO(b,t) (((b) << 4) | ((t) & 0xf))

#define ELF64_ST_BIND(i)   ELF32_ST_BIND(i)
#define ELF64_ST_TYPE(i)   ELF32_ST_TYPE(i)
#define ELF64_ST_INFO(b,t) ELF32_ST_INFO(b,t)

#endif /* _ELF_H */
