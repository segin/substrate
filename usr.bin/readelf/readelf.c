#include <elfobj.h>
#include <demangle.h>

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READELF_VERSION "0.1.0"
#define EI_NIDENT 16
#define EI_DATA 5

#ifndef ELFDATA2LSB
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
#endif

#ifndef EF_ARM_RELEXEC
#define EF_ARM_RELEXEC 0x01u
#define EF_ARM_HASENTRY 0x02u
#define EF_ARM_INTERWORK 0x04u
#define EF_ARM_APCS_26 0x08u
#define EF_ARM_APCS_FLOAT 0x10u
#define EF_ARM_PIC 0x20u
#define EF_ARM_ALIGN8 0x40u
#define EF_ARM_NEW_ABI 0x80u
#define EF_ARM_OLD_ABI 0x100u
#define EF_ARM_SOFT_FLOAT 0x200u
#define EF_ARM_VFP_FLOAT 0x400u
#define EF_ARM_MAVERICK_FLOAT 0x800u
#define EF_ARM_BE8 0x00800000u
#define EF_ARM_LE8 0x00400000u
#define EF_ARM_EABIMASK 0xFF000000u
#define EF_ARM_EABI_VER1 0x01000000u
#define EF_ARM_EABI_VER2 0x02000000u
#define EF_ARM_EABI_VER3 0x03000000u
#define EF_ARM_EABI_VER4 0x04000000u
#define EF_ARM_EABI_VER5 0x05000000u
#endif

#ifndef SHT_INIT_ARRAY
#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15
#define SHT_PREINIT_ARRAY 16
#define SHT_GROUP 17
#define SHT_SYMTAB_SHNDX 18
#endif

#ifndef SHT_GNU_HASH
#define SHT_GNU_ATTRIBUTES 0x6ffffff5
#define SHT_GNU_HASH 0x6ffffff6
#define SHT_GNU_verdef 0x6ffffffd
#define SHT_GNU_verneed 0x6ffffffe
#define SHT_GNU_versym 0x6fffffff
#endif

#ifndef SHT_ARM_EXIDX
#define SHT_ARM_EXIDX 0x70000001
#define SHT_ARM_PREEMPTMAP 0x70000002
#define SHT_ARM_ATTRIBUTES 0x70000003
#define SHT_AARCH64_ATTRIBUTES 0x70000003
#endif

#ifndef SHF_INFO_LINK
#define SHF_INFO_LINK 0x40
#define SHF_LINK_ORDER 0x80
#define SHF_OS_NONCONFORMING 0x100
#define SHF_GROUP 0x200
#define SHF_TLS 0x400
#define SHF_EXCLUDE 0x80000000u
#endif

#ifndef SHF_MASKOS
#define SHF_MASKOS 0x0ff00000u
#define SHF_MASKPROC 0xf0000000u
#endif

#ifndef SHF_ARM_PURECODE
#define SHF_ARM_PURECODE 0x20000000u
#endif

#ifndef PT_GNU_EH_FRAME
#define PT_GNU_EH_FRAME 0x6474e550u
#define PT_GNU_STACK 0x6474e551u
#define PT_GNU_RELRO 0x6474e552u
#define PT_GNU_PROPERTY 0x6474e553u
#endif

#ifndef PT_ARM_EXIDX
#define PT_ARM_EXIDX 0x70000001u
#endif

#ifndef PT_AARCH64_MEMTAG_MTE
#define PT_AARCH64_MEMTAG_MTE 0x70000002u
#endif

#ifndef PF_X
#define PF_X 0x1u
#define PF_W 0x2u
#define PF_R 0x4u
#endif

#ifndef STB_GNU_UNIQUE
#define STB_GNU_UNIQUE 10
#endif

#ifndef STT_GNU_IFUNC
#define STT_GNU_IFUNC 10
#endif

#ifndef STV_DEFAULT
#define STV_DEFAULT 0
#define STV_INTERNAL 1
#define STV_HIDDEN 2
#define STV_PROTECTED 3
#endif

#ifndef SHN_XINDEX
#define SHN_XINDEX 0xffff
#endif

#ifndef DT_SONAME
#define DT_SONAME 14
#define DT_RPATH 15
#define DT_SYMBOLIC 16
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_DEBUG 21
#define DT_TEXTREL 22
#define DT_JMPREL 23
#define DT_BIND_NOW 24
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH 29
#define DT_FLAGS 30
#define DT_PREINIT_ARRAY 32
#define DT_PREINIT_ARRAYSZ 33
#define DT_GNU_HASH 0x6ffffef5
#define DT_FLAGS_1 0x6ffffffb
#define DT_VERDEF 0x6ffffffc
#define DT_VERDEFNUM 0x6ffffffd
#define DT_VERNEED 0x6ffffffe
#define DT_VERNEEDNUM 0x6fffffff
#define DT_VERSYM 0x6ffffff0
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_PLTGOT 3
#define DT_PLTRELSZ 2
#define DT_HASH 4
#endif

#ifndef DF_ORIGIN
#define DF_ORIGIN 0x00000001u
#define DF_SYMBOLIC 0x00000002u
#define DF_TEXTREL 0x00000004u
#define DF_BIND_NOW 0x00000008u
#define DF_STATIC_TLS 0x00000010u
#endif

#ifndef DF_1_NOW
#define DF_1_NOW 0x00000001u
#define DF_1_GLOBAL 0x00000002u
#define DF_1_NODELETE 0x00000008u
#define DF_1_LOADFLTR 0x00000010u
#define DF_1_INITFIRST 0x00000020u
#define DF_1_NOOPEN 0x00000040u
#define DF_1_ORIGIN 0x00000080u
#define DF_1_INTERPOSE 0x00000400u
#define DF_1_NODEFLIB 0x00000800u
#define DF_1_NODUMP 0x00001000u
#define DF_1_PIE 0x08000000u
#endif

#ifndef NT_GNU_ABI_TAG
#define NT_GNU_ABI_TAG 1
#define NT_GNU_HWCAP 2
#define NT_GNU_BUILD_ID 3
#define NT_GNU_GOLD_VERSION 4
#define NT_GNU_PROPERTY_TYPE_0 5
#endif

#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#define NT_FPREGSET 2
#define NT_PRPSINFO 3
#define NT_AUXV 6
#define NT_FILE 0x46494c45
#endif

#ifndef GNU_PROPERTY_STACK_SIZE
#define GNU_PROPERTY_STACK_SIZE 1u
#define GNU_PROPERTY_NO_COPY_ON_PROTECTED 2u
#define GNU_PROPERTY_X86_ISA_1_USED 0xc0010002u
#define GNU_PROPERTY_X86_ISA_1_NEEDED 0xc0008002u
#define GNU_PROPERTY_X86_FEATURE_1_AND 0xc0000002u
#define GNU_PROPERTY_AARCH64_FEATURE_1_AND 0xc0000000u
#endif

#ifndef GRP_COMDAT
#define GRP_COMDAT 0x1u
#endif

typedef struct {
    int wide;
    int show_file_header;
    int show_program_headers;
    int show_section_headers;
    int show_symbols;
    int only_dynsyms;
    int show_relocs;
    int show_dynamic;
    int show_notes;
    int show_version_info;
    int show_histogram;
    int show_groups;
    int show_unwind;
    int show_arch_specific;
    const char *hex_dump_target;
    const char *str_dump_target;
    int demangle_names;
    int use_dynamic_for_symbols;
    int section_details;
    int print_sysv;
    int sym_base;
    int show_debug_dump;
    const char *debug_dump_kind;
    int show_core;
} readelf_opts_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    elfobj_class_t cls;
    elfobj_endian_t endian;
} elf_view_t;

typedef struct {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
    int truncated;
} elf_header_t;

typedef struct {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t off;
    uint64_t size;
    uint64_t entsize;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
} section_header_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t off;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} program_header_t;

static const char *g_progname = "readelf";
static void warnf(const char *fmt, ...);

static int view_u16(const elf_view_t *view, size_t off, uint16_t *out) {
    const uint8_t *p;

    if (view == NULL || out == NULL || off + 2 > view->size) {
        return -1;
    }
    p = view->data + off;
    if (view->endian == ELFOBJ_ENDIAN_BE) {
        *out = ((uint16_t)p[0] << 8) | p[1];
    } else {
        *out = ((uint16_t)p[1] << 8) | p[0];
    }
    return 0;
}

static int view_u32(const elf_view_t *view, size_t off, uint32_t *out) {
    const uint8_t *p;

    if (view == NULL || out == NULL || off + 4 > view->size) {
        return -1;
    }
    p = view->data + off;
    if (view->endian == ELFOBJ_ENDIAN_BE) {
        *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    } else {
        *out = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
    }
    return 0;
}

static int view_u64(const elf_view_t *view, size_t off, uint64_t *out) {
    uint32_t lo;
    uint32_t hi;

    if (view == NULL || out == NULL || off + 8 > view->size) {
        return -1;
    }
    if (view->endian == ELFOBJ_ENDIAN_BE) {
        if (view_u32(view, off, &hi) != 0 || view_u32(view, off + 4, &lo) != 0) {
            return -1;
        }
    } else {
        if (view_u32(view, off, &lo) != 0 || view_u32(view, off + 4, &hi) != 0) {
            return -1;
        }
    }
    *out = ((uint64_t)hi << 32) | lo;
    return 0;
}

static const char *osabi_name(uint8_t osabi) {
    switch (osabi) {
        case ELFOSABI_NONE:
            return "UNIX - System V";
        case ELFOSABI_LINUX:
            return "UNIX - GNU/Linux";
        case ELFOSABI_FREEBSD:
            return "UNIX - FreeBSD";
        case 97:
            return "ARM";
        default:
            return NULL;
    }
}

static const char *type_name(uint16_t type) {
    switch (type) {
        case ET_NONE:
            return "NONE (None)";
        case ET_REL:
            return "REL (Relocatable file)";
        case ET_EXEC:
            return "EXEC (Executable file)";
        case ET_DYN:
            return "DYN (Shared object file)";
        case ET_CORE:
            return "CORE (Core file)";
        default:
            return NULL;
    }
}

static const char *machine_name(uint16_t machine) {
    switch (machine) {
        case EM_386:
            return "Intel 80386";
        case EM_X86_64:
            return "Advanced Micro Devices X86-64";
        case EM_ARM:
            return "ARM";
        case EM_AARCH64:
            return "AArch64";
        default:
            return NULL;
    }
}

static uint64_t shnum_resolved(const elf_header_t *hdr) {
    return hdr->shnum == 0 ? 0 : hdr->shnum;
}

static uint64_t shstrndx_resolved(const elf_header_t *hdr) {
    return hdr->shstrndx;
}

static int read_shdr(const elf_view_t *view, const elf_header_t *hdr, size_t index,
                     section_header_t *out) {
    size_t off;

    if (view == NULL || hdr == NULL || out == NULL) {
        return -1;
    }
    if (hdr->shentsize == 0) {
        return -1;
    }
    off = (size_t)hdr->shoff + ((size_t)hdr->shentsize * index);
    memset(out, 0, sizeof(*out));

    if (view->cls == ELFOBJ_CLASS_64) {
        if (view_u32(view, off + 0, &out->name) != 0) return -1;
        if (view_u32(view, off + 4, &out->type) != 0) return -1;
        if (view_u64(view, off + 8, &out->flags) != 0) return -1;
        if (view_u64(view, off + 16, &out->addr) != 0) return -1;
        if (view_u64(view, off + 24, &out->off) != 0) return -1;
        if (view_u64(view, off + 32, &out->size) != 0) return -1;
        if (view_u32(view, off + 40, &out->link) != 0) return -1;
        if (view_u32(view, off + 44, &out->info) != 0) return -1;
        if (view_u64(view, off + 48, &out->addralign) != 0) return -1;
        if (view_u64(view, off + 56, &out->entsize) != 0) return -1;
    } else {
        uint32_t v = 0;

        if (view_u32(view, off + 0, &out->name) != 0) return -1;
        if (view_u32(view, off + 4, &out->type) != 0) return -1;
        if (view_u32(view, off + 8, &v) != 0) return -1;
        out->flags = v;
        if (view_u32(view, off + 12, &v) != 0) return -1;
        out->addr = v;
        if (view_u32(view, off + 16, &v) != 0) return -1;
        out->off = v;
        if (view_u32(view, off + 20, &v) != 0) return -1;
        out->size = v;
        if (view_u32(view, off + 24, &out->link) != 0) return -1;
        if (view_u32(view, off + 28, &out->info) != 0) return -1;
        if (view_u32(view, off + 32, &v) != 0) return -1;
        out->addralign = v;
        if (view_u32(view, off + 36, &v) != 0) return -1;
        out->entsize = v;
    }
    return 0;
}

static const char *section_type_name(uint16_t machine, uint32_t type) {
    if (type == SHT_ARM_ATTRIBUTES) {
        return machine == EM_AARCH64 ? "AARCH64_ATTRIBUTES" : "ARM_ATTRIBUTES";
    }
    switch (type) {
        case SHT_NULL: return "NULL";
        case SHT_PROGBITS: return "PROGBITS";
        case SHT_SYMTAB: return "SYMTAB";
        case SHT_STRTAB: return "STRTAB";
        case SHT_RELA: return "RELA";
        case SHT_HASH: return "HASH";
        case SHT_DYNAMIC: return "DYNAMIC";
        case SHT_NOTE: return "NOTE";
        case SHT_NOBITS: return "NOBITS";
        case SHT_REL: return "REL";
        case SHT_DYNSYM: return "DYNSYM";
        case SHT_INIT_ARRAY: return "INIT_ARRAY";
        case SHT_FINI_ARRAY: return "FINI_ARRAY";
        case SHT_PREINIT_ARRAY: return "PREINIT_ARRAY";
        case SHT_GROUP: return "GROUP";
        case SHT_SYMTAB_SHNDX: return "SYMTAB_SHNDX";
        case SHT_GNU_HASH: return "GNU_HASH";
        case SHT_GNU_verdef: return "GNU_verdef";
        case SHT_GNU_verneed: return "GNU_verneed";
        case SHT_GNU_versym: return "GNU_versym";
        case SHT_ARM_EXIDX: return "ARM_EXIDX";
        case SHT_ARM_PREEMPTMAP: return "ARM_PREEMPTMAP";
        case SHT_ARM_ATTRIBUTES: return "ARM_ATTRIBUTES";
        default: return NULL;
    }
}

static void section_flags_letters(uint16_t machine, uint64_t flags, char *buf, size_t buflen) {
    size_t n = 0;
#define APPEND_CH(ch) \
    do { \
        if (n + 1 < buflen) { \
            buf[n++] = (ch); \
        } \
    } while (0)

    if (buflen == 0) {
        return;
    }
    if (flags & SHF_WRITE) APPEND_CH('W');
    if (flags & SHF_ALLOC) APPEND_CH('A');
    if (flags & SHF_EXECINSTR) APPEND_CH('X');
    if (flags & SHF_MERGE) APPEND_CH('M');
    if (flags & SHF_STRINGS) APPEND_CH('S');
    if (flags & SHF_INFO_LINK) APPEND_CH('I');
    if (flags & SHF_LINK_ORDER) APPEND_CH('L');
    if (flags & SHF_OS_NONCONFORMING) APPEND_CH('O');
    if (flags & SHF_GROUP) APPEND_CH('G');
    if (flags & SHF_TLS) APPEND_CH('T');
    if (flags & SHF_COMPRESSED) APPEND_CH('C');
    if (flags & SHF_EXCLUDE) APPEND_CH('E');
    if (machine == EM_ARM && (flags & SHF_ARM_PURECODE)) APPEND_CH('y');
    if (flags & SHF_MASKOS) APPEND_CH('o');
    if (flags & SHF_MASKPROC) APPEND_CH('p');
    buf[n] = '\0';
#undef APPEND_CH
}

static const char *shstr_name(const uint8_t *tab, size_t tabsz, uint32_t off) {
    if (tab == NULL || off >= tabsz) {
        return "<corrupt>";
    }
    return (const char *)(tab + off);
}

static void print_section_headers(const readelf_opts_t *opts, const elf_view_t *view,
                                  const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    uint64_t shstrndx = shstrndx_resolved(hdr);
    uint8_t *shstr_data = NULL;
    size_t shstr_size = 0;
    section_header_t shstr;
    uint64_t i;

    printf("There are %" PRIu64 " section headers, starting at offset 0x%" PRIx64 ":\n",
           shnum, hdr->shoff);
    printf("\nSection Headers:\n");
    if (view->cls == ELFOBJ_CLASS_64) {
        printf("  [Nr] Name              Type             Address           Offset\n");
        printf("       Size              EntSize          Flags  Link  Info  Align\n");
    } else {
        printf("  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al\n");
    }

    if (shstrndx < shnum && read_shdr(view, hdr, (size_t)shstrndx, &shstr) == 0) {
        if (shstr.off < view->size && shstr.off + shstr.size <= view->size) {
            shstr_data = (uint8_t *)(uintptr_t)(view->data + shstr.off);
            shstr_size = (size_t)shstr.size;
        }
    }

    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        const char *type_name_local;
        char type_buf[32];
        char flg[32];
        const char *name;
        char trunc_name[20];

        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) {
            warnf("section header %" PRIu64 " is truncated", i);
            break;
        }
        type_name_local = section_type_name(hdr->machine, sh.type);
        if (type_name_local == NULL) {
            snprintf(type_buf, sizeof(type_buf), "0x%x", sh.type);
            type_name_local = type_buf;
        }
        section_flags_letters(hdr->machine, sh.flags, flg, sizeof(flg));
        name = shstr_name(shstr_data, shstr_size, sh.name);

        if (!opts->wide && strlen(name) > 17) {
            memcpy(trunc_name, name, 17);
            trunc_name[17] = '\0';
            name = trunc_name;
        }

        if (view->cls == ELFOBJ_CLASS_64) {
            printf("  [%2" PRIu64 "] %-17s %-16s %016" PRIx64 " %08" PRIx64 "\n",
                   i, name, type_name_local, sh.addr, sh.off);
            printf("       %016" PRIx64 " %016" PRIx64 " %-5s %5u %5u %5" PRIu64 "\n",
                   sh.size, sh.entsize, flg, sh.link, sh.info, sh.addralign);
        } else {
            printf("  [%2" PRIu64 "] %-17s %-15s %08" PRIx64 " %06" PRIx64 " %06" PRIx64 " %02" PRIx64
                   " %-3s %2u %3u %2" PRIu64 "\n",
                   i, name, type_name_local, sh.addr, sh.off, sh.size, sh.entsize,
                   flg, sh.link, sh.info, sh.addralign);
        }
    }

    printf("Key to Flags:\n");
    printf("  W (write), A (alloc), X (execute), M (merge), S (strings), I (info),\n");
    printf("  L (link order), O (extra OS processing required), G (group), T (TLS),\n");
    printf("  C (compressed), y (purecode), o (OS specific), E (exclude), p (processor specific)\n");
}

static int read_phdr(const elf_view_t *view, const elf_header_t *hdr, size_t index,
                     program_header_t *out) {
    size_t off;

    if (view == NULL || hdr == NULL || out == NULL || hdr->phentsize == 0) {
        return -1;
    }
    off = (size_t)hdr->phoff + ((size_t)hdr->phentsize * index);
    memset(out, 0, sizeof(*out));
    if (view->cls == ELFOBJ_CLASS_64) {
        if (view_u32(view, off + 0, &out->type) != 0) return -1;
        if (view_u32(view, off + 4, &out->flags) != 0) return -1;
        if (view_u64(view, off + 8, &out->off) != 0) return -1;
        if (view_u64(view, off + 16, &out->vaddr) != 0) return -1;
        if (view_u64(view, off + 24, &out->paddr) != 0) return -1;
        if (view_u64(view, off + 32, &out->filesz) != 0) return -1;
        if (view_u64(view, off + 40, &out->memsz) != 0) return -1;
        if (view_u64(view, off + 48, &out->align) != 0) return -1;
    } else {
        uint32_t v = 0;

        if (view_u32(view, off + 0, &out->type) != 0) return -1;
        if (view_u32(view, off + 4, &v) != 0) return -1;
        out->off = v;
        if (view_u32(view, off + 8, &v) != 0) return -1;
        out->vaddr = v;
        if (view_u32(view, off + 12, &v) != 0) return -1;
        out->paddr = v;
        if (view_u32(view, off + 16, &v) != 0) return -1;
        out->filesz = v;
        if (view_u32(view, off + 20, &v) != 0) return -1;
        out->memsz = v;
        if (view_u32(view, off + 24, &out->flags) != 0) return -1;
        if (view_u32(view, off + 28, &v) != 0) return -1;
        out->align = v;
    }
    return 0;
}

static const char *segment_type_name(uint32_t type) {
    switch (type) {
        case PT_NULL: return "NULL";
        case PT_LOAD: return "LOAD";
        case PT_DYNAMIC: return "DYNAMIC";
        case PT_INTERP: return "INTERP";
        case PT_NOTE: return "NOTE";
        case 5: return "SHLIB";
        case PT_PHDR: return "PHDR";
        case PT_TLS: return "TLS";
        case PT_GNU_EH_FRAME: return "GNU_EH_FRAME";
        case PT_GNU_STACK: return "GNU_STACK";
        case PT_GNU_RELRO: return "GNU_RELRO";
        case PT_GNU_PROPERTY: return "GNU_PROPERTY";
        case PT_ARM_EXIDX: return "ARM_EXIDX";
        case PT_AARCH64_MEMTAG_MTE: return "AARCH64_MEMTAG_MTE";
        default: return NULL;
    }
}

static void segment_flags_letters(uint32_t flags, char out[4]) {
    out[0] = (flags & PF_R) ? 'R' : ' ';
    out[1] = (flags & PF_W) ? 'W' : ' ';
    out[2] = (flags & PF_X) ? 'E' : ' ';
    out[3] = '\0';
}

static void print_section_to_segment(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    uint64_t shstrndx = shstrndx_resolved(hdr);
    section_header_t shstr;
    const uint8_t *shstr_data = NULL;
    size_t shstr_size = 0;
    uint64_t i;

    if (shstrndx < shnum && read_shdr(view, hdr, (size_t)shstrndx, &shstr) == 0 &&
        shstr.off + shstr.size <= view->size) {
        shstr_data = view->data + shstr.off;
        shstr_size = (size_t)shstr.size;
    }

    printf("\n Section to Segment mapping:\n");
    printf("  Segment Sections...\n");
    for (i = 0; i < hdr->phnum; ++i) {
        program_header_t ph;
        uint64_t sidx;

        if (read_phdr(view, hdr, (size_t)i, &ph) != 0) {
            continue;
        }
        printf("   %2" PRIu64 "     ", i);
        for (sidx = 0; sidx < shnum; ++sidx) {
            section_header_t sh;
            const char *name;
            uint64_t sh_end;
            uint64_t ph_end;

            if (read_shdr(view, hdr, (size_t)sidx, &sh) != 0) {
                continue;
            }
            if ((sh.flags & SHF_ALLOC) == 0 || sh.size == 0) {
                continue;
            }
            sh_end = sh.addr + sh.size;
            ph_end = ph.vaddr + ph.memsz;
            if (sh.addr >= ph.vaddr && sh_end <= ph_end) {
                name = shstr_name(shstr_data, shstr_size, sh.name);
                printf("%s ", name);
            }
        }
        printf("\n");
    }
}

static void print_program_headers(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t i;

    printf("\nProgram Headers:\n");
    if (view->cls == ELFOBJ_CLASS_64) {
        printf("  Type           Offset             VirtAddr           PhysAddr\n");
        printf("                 FileSiz            MemSiz              Flags  Align\n");
    } else {
        printf("  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align\n");
    }

    for (i = 0; i < hdr->phnum; ++i) {
        program_header_t ph;
        const char *name;
        char type_buf[24];
        char flags[4];

        if (read_phdr(view, hdr, (size_t)i, &ph) != 0) {
            warnf("program header %" PRIu64 " is truncated", i);
            break;
        }
        name = segment_type_name(ph.type);
        if (name == NULL) {
            snprintf(type_buf, sizeof(type_buf), "0x%x", ph.type);
            name = type_buf;
        }
        segment_flags_letters(ph.flags, flags);
        if (view->cls == ELFOBJ_CLASS_64) {
            printf("  %-14s 0x%016" PRIx64 " 0x%016" PRIx64 " 0x%016" PRIx64 "\n",
                   name, ph.off, ph.vaddr, ph.paddr);
            printf("                 0x%016" PRIx64 " 0x%016" PRIx64 "  %-3s  0x%" PRIx64 "\n",
                   ph.filesz, ph.memsz, flags, ph.align);
        } else {
            printf("  %-14s 0x%06" PRIx64 " 0x%08" PRIx64 " 0x%08" PRIx64 " 0x%05" PRIx64
                   " 0x%05" PRIx64 " %-3s 0x%" PRIx64 "\n",
                   name, ph.off, ph.vaddr, ph.paddr, ph.filesz, ph.memsz, flags, ph.align);
        }

        if (ph.type == PT_INTERP && ph.off < view->size && ph.off + ph.filesz <= view->size) {
            const char *interp = (const char *)(view->data + ph.off);
            printf("      [Requesting program interpreter: %s]\n", interp);
        }
    }

    print_section_to_segment(view, hdr);
}

static const char *sym_type_name(uint8_t type) {
    switch (type) {
        case STT_NOTYPE: return "NOTYPE";
        case STT_OBJECT: return "OBJECT";
        case STT_FUNC: return "FUNC";
        case STT_SECTION: return "SECTION";
        case STT_FILE: return "FILE";
        case 5: return "COMMON";
        case STT_TLS: return "TLS";
        case STT_GNU_IFUNC: return "GNU_IFUNC";
        default: return NULL;
    }
}

static const char *sym_bind_name(uint8_t bind) {
    switch (bind) {
        case STB_LOCAL: return "LOCAL";
        case STB_GLOBAL: return "GLOBAL";
        case STB_WEAK: return "WEAK";
        case STB_GNU_UNIQUE: return "GNU_UNIQUE";
        default: return NULL;
    }
}

static const char *sym_vis_name(uint8_t vis) {
    switch (vis & 0x3) {
        case STV_DEFAULT: return "DEFAULT";
        case STV_INTERNAL: return "INTERNAL";
        case STV_HIDDEN: return "HIDDEN";
        case STV_PROTECTED: return "PROTECTED";
        default: return NULL;
    }
}

static void format_sym_ndx(uint16_t shndx, uint32_t xindex, char *buf, size_t buflen) {
    if (shndx == SHN_UNDEF) {
        snprintf(buf, buflen, "UND");
    } else if (shndx == SHN_ABS) {
        snprintf(buf, buflen, "ABS");
    } else if (shndx == SHN_COMMON) {
        snprintf(buf, buflen, "COM");
    } else if (shndx == SHN_XINDEX) {
        snprintf(buf, buflen, "%u", xindex);
    } else {
        snprintf(buf, buflen, "%u", shndx);
    }
}

static int find_symtab_shndx_section(const elf_view_t *view, const elf_header_t *hdr,
                                     size_t symsec_index, section_header_t *out) {
    uint64_t i;
    section_header_t sh;

    for (i = 0; i < shnum_resolved(hdr); ++i) {
        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) {
            continue;
        }
        if (sh.type == SHT_SYMTAB_SHNDX && sh.link == symsec_index) {
            if (out != NULL) {
                *out = sh;
            }
            return 1;
        }
    }
    return 0;
}

static int read_sym32(const elf_view_t *view, uint64_t base, size_t index,
                      uint64_t entsize, uint32_t *st_name, uint8_t *st_info, uint8_t *st_other,
                      uint16_t *st_shndx, uint64_t *st_value, uint64_t *st_size) {
    uint32_t v = 0;
    uint64_t off = base + (index * entsize);

    if (view_u32(view, (size_t)(off + 0), st_name) != 0) return -1;
    if (view_u32(view, (size_t)(off + 4), &v) != 0) return -1;
    *st_value = v;
    if (view_u32(view, (size_t)(off + 8), &v) != 0) return -1;
    *st_size = v;
    if (off + 16 > view->size) return -1;
    *st_info = view->data[off + 12];
    *st_other = view->data[off + 13];
    if (view_u16(view, (size_t)(off + 14), st_shndx) != 0) return -1;
    return 0;
}

static int read_sym64(const elf_view_t *view, uint64_t base, size_t index,
                      uint64_t entsize, uint32_t *st_name, uint8_t *st_info, uint8_t *st_other,
                      uint16_t *st_shndx, uint64_t *st_value, uint64_t *st_size) {
    uint64_t off = base + (index * entsize);

    if (view_u32(view, (size_t)(off + 0), st_name) != 0) return -1;
    if (off + 24 > view->size) return -1;
    *st_info = view->data[off + 4];
    *st_other = view->data[off + 5];
    if (view_u16(view, (size_t)(off + 6), st_shndx) != 0) return -1;
    if (view_u64(view, (size_t)(off + 8), st_value) != 0) return -1;
    if (view_u64(view, (size_t)(off + 16), st_size) != 0) return -1;
    return 0;
}

static void format_sym_value(uint64_t value, int base, int cls64, char *buf, size_t bufsz) {
    if (base == 10) {
        snprintf(buf, bufsz, "%llu", (unsigned long long)value);
    } else if (base == 8) {
        snprintf(buf, bufsz, "%llo", (unsigned long long)value);
    } else if (base == 0) {
        snprintf(buf, bufsz, "%llu", (unsigned long long)value);
    } else if (cls64) {
        snprintf(buf, bufsz, "%016llx", (unsigned long long)value);
    } else {
        snprintf(buf, bufsz, "%08llx", (unsigned long long)value);
    }
}

static void print_symbol_table_section(const readelf_opts_t *opts,
                                       const elf_view_t *view, const elf_header_t *hdr,
                                       size_t sec_index, const section_header_t *symsec,
                                       const uint8_t *shstr_data, size_t shstr_size) {
    section_header_t strsec;
    section_header_t xsec;
    const uint8_t *strtab = NULL;
    size_t strtabsz = 0;
    const uint8_t *xdata = NULL;
    size_t xcount = 0;
    uint64_t count;
    uint64_t i;
    const char *secname;

    if (symsec->link >= shnum_resolved(hdr) || read_shdr(view, hdr, symsec->link, &strsec) != 0) {
        warnf("symbol table at section %zu has invalid string table link", sec_index);
        return;
    }
    if (strsec.off + strsec.size <= view->size) {
        strtab = view->data + strsec.off;
        strtabsz = (size_t)strsec.size;
    }
    if (find_symtab_shndx_section(view, hdr, sec_index, &xsec) &&
        xsec.off + xsec.size <= view->size) {
        xdata = view->data + xsec.off;
        xcount = (size_t)(xsec.size / 4);
    }

    if (symsec->entsize == 0) {
        return;
    }
    count = symsec->size / symsec->entsize;
    secname = shstr_name(shstr_data, shstr_size, symsec->name);
    printf("\nSymbol table '%s' contains %" PRIu64 " entries:\n", secname, count);
    printf("   Num:    Value          Size Type    Bind   Vis      Ndx Name\n");

    for (i = 0; i < count; ++i) {
        uint32_t st_name = 0;
        uint8_t st_info = 0;
        uint8_t st_other = 0;
        uint16_t st_shndx = 0;
        uint64_t st_value = 0;
        uint64_t st_size = 0;
        uint8_t type;
        uint8_t bind;
        const char *type_name_local;
        const char *bind_name_local;
        const char *vis_name_local;
        char type_buf[16];
        char bind_buf[16];
        char vis_buf[16];
        char ndx_buf[16];
        uint32_t xindex = 0;
        const char *name = "<corrupt>";
        const char *print_name = NULL;
        char *dem = NULL;
        char note[24];
        char value_buf[32];
        note[0] = '\0';

        if (view->cls == ELFOBJ_CLASS_64) {
            if (read_sym64(view, symsec->off, (size_t)i, symsec->entsize, &st_name,
                           &st_info, &st_other, &st_shndx, &st_value, &st_size) != 0) {
                warnf("symbol entry %" PRIu64 " is truncated", i);
                break;
            }
        } else {
            if (read_sym32(view, symsec->off, (size_t)i, symsec->entsize, &st_name,
                           &st_info, &st_other, &st_shndx, &st_value, &st_size) != 0) {
                warnf("symbol entry %" PRIu64 " is truncated", i);
                break;
            }
        }

        if (st_name < strtabsz) {
            name = (const char *)(strtab + st_name);
        }
        print_name = name;
        if (opts->demangle_names && name[0] != '\0') {
            dem = demangle(name, DEMANGLE_AUTO);
            if (dem != NULL) {
                print_name = dem;
            }
        }
        if (st_shndx == SHN_XINDEX && i < xcount) {
            if (view_u32(&(elf_view_t){.data = xdata, .size = xsec.size, .endian = view->endian},
                         (size_t)(i * 4), &xindex) != 0) {
                xindex = 0;
            }
        }

        type = st_info & 0x0f;
        bind = st_info >> 4;
        type_name_local = sym_type_name(type);
        if (type_name_local == NULL) {
            snprintf(type_buf, sizeof(type_buf), "%u", type);
            type_name_local = type_buf;
        }
        bind_name_local = sym_bind_name(bind);
        if (bind_name_local == NULL) {
            snprintf(bind_buf, sizeof(bind_buf), "%u", bind);
            bind_name_local = bind_buf;
        }
        vis_name_local = sym_vis_name(st_other);
        if (vis_name_local == NULL) {
            snprintf(vis_buf, sizeof(vis_buf), "%u", st_other & 0x3);
            vis_name_local = vis_buf;
        }
        format_sym_ndx(st_shndx, xindex, ndx_buf, sizeof(ndx_buf));

        if (hdr->machine == EM_ARM && type == STT_FUNC && (st_value & 1u)) {
            snprintf(note, sizeof(note), " [Thumb]");
        } else if (hdr->machine == EM_ARM &&
                   (strcmp(name, "$a") == 0 || strcmp(name, "$t") == 0 || strcmp(name, "$d") == 0)) {
            snprintf(note, sizeof(note), " [mapping]");
        } else if (hdr->machine == EM_AARCH64 &&
                   (strcmp(name, "$x") == 0 || strcmp(name, "$d") == 0)) {
            snprintf(note, sizeof(note), " [mapping]");
        }

        format_sym_value(st_value, opts->sym_base, view->cls == ELFOBJ_CLASS_64,
                         value_buf, sizeof(value_buf));
        printf("%6" PRIu64 ": %16s %5" PRIu64 " %-7s %-6s %-8s %3s %s%s\n",
               i, value_buf, st_size, type_name_local, bind_name_local,
               vis_name_local, ndx_buf, print_name, note);
        if (dem != NULL) {
            demangle_free(dem);
        }
    }
}

static void print_symbol_tables(const readelf_opts_t *opts, const elf_view_t *view,
                                const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    uint64_t shstrndx = shstrndx_resolved(hdr);
    section_header_t shstr;
    const uint8_t *shstr_data = NULL;
    size_t shstr_size = 0;
    uint64_t i;

    if (shstrndx < shnum && read_shdr(view, hdr, (size_t)shstrndx, &shstr) == 0 &&
        shstr.off + shstr.size <= view->size) {
        shstr_data = view->data + shstr.off;
        shstr_size = (size_t)shstr.size;
    }

    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        int is_symtab;

        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) {
            break;
        }
        if (opts->only_dynsyms || opts->use_dynamic_for_symbols) {
            is_symtab = (sh.type == SHT_DYNSYM);
        } else {
            is_symtab = (sh.type == SHT_SYMTAB || sh.type == SHT_DYNSYM);
        }
        if (!is_symtab) {
            continue;
        }
        print_symbol_table_section(opts, view, hdr, (size_t)i, &sh, shstr_data, shstr_size);
    }
}

static const char *reloc_type_i386(uint32_t type) {
    switch (type) {
        case 0: return "R_386_NONE";
        case 1: return "R_386_32";
        case 2: return "R_386_PC32";
        case 3: return "R_386_GOT32";
        case 4: return "R_386_PLT32";
        case 5: return "R_386_COPY";
        case 6: return "R_386_GLOB_DAT";
        case 7: return "R_386_JMP_SLOT";
        case 8: return "R_386_RELATIVE";
        case 9: return "R_386_GOTOFF";
        case 10: return "R_386_GOTPC";
        case 14: return "R_386_TLS_TPOFF";
        case 15: return "R_386_TLS_IE";
        case 16: return "R_386_TLS_GOTIE";
        case 17: return "R_386_TLS_LE";
        case 18: return "R_386_TLS_GD";
        case 19: return "R_386_TLS_LDM";
        case 20: return "R_386_16";
        case 21: return "R_386_PC16";
        case 22: return "R_386_8";
        case 23: return "R_386_PC8";
        case 32: return "R_386_TLS_LDO_32";
        case 35: return "R_386_TLS_DTPMOD32";
        case 36: return "R_386_TLS_DTPOFF32";
        case 38: return "R_386_SIZE32";
        case 42: return "R_386_IRELATIVE";
        case 43: return "R_386_GOT32X";
        default: return NULL;
    }
}

static const char *reloc_type_x86_64(uint32_t type) {
    switch (type) {
        case 0: return "R_X86_64_NONE";
        case 1: return "R_X86_64_64";
        case 2: return "R_X86_64_PC32";
        case 3: return "R_X86_64_GOT32";
        case 4: return "R_X86_64_PLT32";
        case 5: return "R_X86_64_COPY";
        case 6: return "R_X86_64_GLOB_DAT";
        case 7: return "R_X86_64_JUMP_SLOT";
        case 8: return "R_X86_64_RELATIVE";
        case 9: return "R_X86_64_GOTPCREL";
        case 10: return "R_X86_64_32";
        case 11: return "R_X86_64_32S";
        case 12: return "R_X86_64_16";
        case 13: return "R_X86_64_PC16";
        case 14: return "R_X86_64_8";
        case 15: return "R_X86_64_PC8";
        case 16: return "R_X86_64_DTPMOD64";
        case 17: return "R_X86_64_DTPOFF64";
        case 18: return "R_X86_64_TPOFF64";
        case 19: return "R_X86_64_TLSGD";
        case 20: return "R_X86_64_TLSLD";
        case 21: return "R_X86_64_DTPOFF32";
        case 22: return "R_X86_64_GOTTPOFF";
        case 23: return "R_X86_64_TPOFF32";
        case 24: return "R_X86_64_PC64";
        case 25: return "R_X86_64_GOTOFF64";
        case 26: return "R_X86_64_GOTPC32";
        case 32: return "R_X86_64_SIZE32";
        case 33: return "R_X86_64_SIZE64";
        case 37: return "R_X86_64_IRELATIVE";
        case 41: return "R_X86_64_GOTPCRELX";
        case 42: return "R_X86_64_REX_GOTPCRELX";
        default: return NULL;
    }
}

static const char *reloc_type_arm(uint32_t type) {
    switch (type) {
        case 0: return "R_ARM_NONE";
        case 1: return "R_ARM_PC24";
        case 2: return "R_ARM_ABS32";
        case 3: return "R_ARM_REL32";
        case 4: return "R_ARM_LDR_PC_G0";
        case 5: return "R_ARM_ABS16";
        case 6: return "R_ARM_ABS12";
        case 7: return "R_ARM_THM_ABS5";
        case 8: return "R_ARM_ABS8";
        case 9: return "R_ARM_SBREL32";
        case 10: return "R_ARM_THM_CALL";
        case 11: return "R_ARM_THM_PC8";
        case 12: return "R_ARM_BREL_ADJ";
        case 13: return "R_ARM_TLS_DESC";
        case 14: return "R_ARM_THM_SWI8";
        case 15: return "R_ARM_XPC25";
        case 16: return "R_ARM_THM_XPC22";
        case 17: return "R_ARM_TLS_DTPMOD32";
        case 18: return "R_ARM_TLS_DTPOFF32";
        case 19: return "R_ARM_TLS_TPOFF32";
        case 20: return "R_ARM_COPY";
        case 21: return "R_ARM_GLOB_DAT";
        case 22: return "R_ARM_JUMP_SLOT";
        case 23: return "R_ARM_RELATIVE";
        case 24: return "R_ARM_GOTOFF32";
        case 25: return "R_ARM_BASE_PREL";
        case 26: return "R_ARM_GOT_BREL";
        case 27: return "R_ARM_PLT32";
        case 28: return "R_ARM_CALL";
        case 29: return "R_ARM_JUMP24";
        case 30: return "R_ARM_THM_JUMP24";
        case 31: return "R_ARM_BASE_ABS";
        case 32: return "R_ARM_ALU_PCREL_7_0";
        case 33: return "R_ARM_ALU_PCREL_15_8";
        case 34: return "R_ARM_ALU_PCREL_23_16";
        case 35: return "R_ARM_LDR_SBREL_11_0_NC";
        case 36: return "R_ARM_ALU_SBREL_19_12_NC";
        case 37: return "R_ARM_ALU_SBREL_27_20_CK";
        case 38: return "R_ARM_TARGET1";
        case 39: return "R_ARM_SBREL31";
        case 40: return "R_ARM_V4BX";
        case 41: return "R_ARM_TARGET2";
        case 42: return "R_ARM_PREL31";
        case 43: return "R_ARM_MOVW_ABS_NC";
        case 44: return "R_ARM_MOVT_ABS";
        case 45: return "R_ARM_MOVW_PREL_NC";
        case 46: return "R_ARM_MOVT_PREL";
        case 47: return "R_ARM_THM_MOVW_ABS_NC";
        case 48: return "R_ARM_THM_MOVT_ABS";
        case 49: return "R_ARM_THM_MOVW_PREL_NC";
        case 50: return "R_ARM_THM_MOVT_PREL";
        case 51: return "R_ARM_THM_JUMP19";
        case 52: return "R_ARM_THM_JUMP6";
        case 53: return "R_ARM_THM_ALU_PREL_11_0";
        case 54: return "R_ARM_THM_PC12";
        case 55: return "R_ARM_ABS32_NOI";
        case 56: return "R_ARM_REL32_NOI";
        case 57: return "R_ARM_ALU_PC_G0_NC";
        case 58: return "R_ARM_ALU_PC_G0";
        case 59: return "R_ARM_ALU_PC_G1_NC";
        case 60: return "R_ARM_ALU_PC_G1";
        case 61: return "R_ARM_ALU_PC_G2";
        case 62: return "R_ARM_LDR_PC_G1";
        case 63: return "R_ARM_LDR_PC_G2";
        case 64: return "R_ARM_LDRS_PC_G0";
        case 65: return "R_ARM_LDRS_PC_G1";
        case 66: return "R_ARM_LDRS_PC_G2";
        case 67: return "R_ARM_LDC_PC_G0";
        case 68: return "R_ARM_LDC_PC_G1";
        case 69: return "R_ARM_LDC_PC_G2";
        case 70: return "R_ARM_ALU_SB_G0_NC";
        case 71: return "R_ARM_ALU_SB_G0";
        case 72: return "R_ARM_ALU_SB_G1_NC";
        case 73: return "R_ARM_ALU_SB_G1";
        case 74: return "R_ARM_ALU_SB_G2";
        case 75: return "R_ARM_LDR_SB_G0";
        case 76: return "R_ARM_LDR_SB_G1";
        case 77: return "R_ARM_LDR_SB_G2";
        case 78: return "R_ARM_LDRS_SB_G0";
        case 79: return "R_ARM_LDRS_SB_G1";
        case 80: return "R_ARM_LDRS_SB_G2";
        case 81: return "R_ARM_LDC_SB_G0";
        case 82: return "R_ARM_LDC_SB_G1";
        case 83: return "R_ARM_LDC_SB_G2";
        case 84: return "R_ARM_MOVW_BREL_NC";
        case 85: return "R_ARM_MOVT_BREL";
        case 86: return "R_ARM_MOVW_BREL";
        case 87: return "R_ARM_THM_MOVW_BREL_NC";
        case 88: return "R_ARM_THM_MOVT_BREL";
        case 89: return "R_ARM_THM_MOVW_BREL";
        case 90: return "R_ARM_TLS_GOTDESC";
        case 91: return "R_ARM_TLS_CALL";
        case 92: return "R_ARM_TLS_DESCSEQ";
        case 93: return "R_ARM_THM_TLS_CALL";
        case 94: return "R_ARM_PLT32_ABS";
        case 95: return "R_ARM_GOT_ABS";
        case 96: return "R_ARM_GOT_PREL";
        case 97: return "R_ARM_GOT_BREL12";
        case 98: return "R_ARM_GOTOFF12";
        case 99: return "R_ARM_GOTRELAX";
        case 100: return "R_ARM_GNU_VTENTRY";
        case 101: return "R_ARM_GNU_VTINHERIT";
        case 102: return "R_ARM_THM_JUMP11";
        case 103: return "R_ARM_THM_JUMP8";
        case 104: return "R_ARM_TLS_GD32";
        case 105: return "R_ARM_TLS_LDM32";
        case 106: return "R_ARM_TLS_LDO32";
        case 107: return "R_ARM_TLS_IE32";
        case 108: return "R_ARM_TLS_LE32";
        case 109: return "R_ARM_TLS_LDO12";
        case 110: return "R_ARM_TLS_LE12";
        case 111: return "R_ARM_TLS_IE12GP";
        case 160: return "R_ARM_IRELATIVE";
        case 249: return "R_ARM_RXPC25";
        case 250: return "R_ARM_RSBREL32";
        case 251: return "R_ARM_THM_RPC22";
        case 252: return "R_ARM_RREL32";
        case 253: return "R_ARM_RABS32";
        case 254: return "R_ARM_RPC24";
        case 255: return "R_ARM_RBASE";
        default: return NULL;
    }
}

static const char *reloc_type_aarch64(uint32_t type) {
    switch (type) {
        case 0: return "R_AARCH64_NONE";
        case 257: return "R_AARCH64_ABS64";
        case 258: return "R_AARCH64_ABS32";
        case 259: return "R_AARCH64_ABS16";
        case 260: return "R_AARCH64_PREL64";
        case 261: return "R_AARCH64_PREL32";
        case 262: return "R_AARCH64_PREL16";
        case 273: return "R_AARCH64_LD_PREL_LO19";
        case 274: return "R_AARCH64_ADR_PREL_LO21";
        case 275: return "R_AARCH64_ADR_PREL_PG_HI21";
        case 276: return "R_AARCH64_ADR_PREL_PG_HI21_NC";
        case 277: return "R_AARCH64_ADD_ABS_LO12_NC";
        case 278: return "R_AARCH64_LDST8_ABS_LO12_NC";
        case 279: return "R_AARCH64_TSTBR14";
        case 280: return "R_AARCH64_CONDBR19";
        case 281: return "R_AARCH64_JUMP26";
        case 282: return "R_AARCH64_CALL26";
        case 283: return "R_AARCH64_LDST16_ABS_LO12_NC";
        case 284: return "R_AARCH64_LDST32_ABS_LO12_NC";
        case 285: return "R_AARCH64_LDST64_ABS_LO12_NC";
        case 286: return "R_AARCH64_LDST128_ABS_LO12_NC";
        case 299: return "R_AARCH64_MOVW_GOTOFF_G0";
        case 300: return "R_AARCH64_MOVW_GOTOFF_G0_NC";
        case 301: return "R_AARCH64_MOVW_GOTOFF_G1";
        case 302: return "R_AARCH64_MOVW_GOTOFF_G1_NC";
        case 303: return "R_AARCH64_MOVW_GOTOFF_G2";
        case 304: return "R_AARCH64_MOVW_GOTOFF_G2_NC";
        case 305: return "R_AARCH64_MOVW_GOTOFF_G3";
        case 1024: return "R_AARCH64_COPY";
        case 1025: return "R_AARCH64_GLOB_DAT";
        case 1026: return "R_AARCH64_JUMP_SLOT";
        case 1027: return "R_AARCH64_RELATIVE";
        case 1028: return "R_AARCH64_TLS_DTPMOD";
        case 1029: return "R_AARCH64_TLS_DTPREL";
        case 1030: return "R_AARCH64_TLS_TPREL";
        case 1031: return "R_AARCH64_TLSDESC";
        case 1032: return "R_AARCH64_IRELATIVE";
        case 512: return "R_AARCH64_TLSGD_ADR_PREL21";
        case 513: return "R_AARCH64_TLSGD_ADR_PAGE21";
        case 514: return "R_AARCH64_TLSGD_ADD_LO12_NC";
        case 515: return "R_AARCH64_TLSGD_MOVW_G1";
        case 516: return "R_AARCH64_TLSGD_MOVW_G0_NC";
        case 517: return "R_AARCH64_TLSLD_ADR_PREL21";
        case 518: return "R_AARCH64_TLSLD_ADR_PAGE21";
        case 519: return "R_AARCH64_TLSLD_ADD_LO12_NC";
        case 520: return "R_AARCH64_TLSLD_MOVW_G1";
        case 521: return "R_AARCH64_TLSLD_MOVW_G0_NC";
        case 522: return "R_AARCH64_TLSLD_LD_PREL19";
        case 523: return "R_AARCH64_TLSLD_MOVW_DTPREL_G2";
        case 524: return "R_AARCH64_TLSLD_MOVW_DTPREL_G1";
        case 525: return "R_AARCH64_TLSLD_MOVW_DTPREL_G1_NC";
        case 526: return "R_AARCH64_TLSLD_MOVW_DTPREL_G0";
        case 527: return "R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC";
        case 528: return "R_AARCH64_TLSLD_ADD_DTPREL_HI12";
        case 529: return "R_AARCH64_TLSLD_ADD_DTPREL_LO12";
        case 530: return "R_AARCH64_TLSLD_ADD_DTPREL_LO12_NC";
        case 531: return "R_AARCH64_TLSLD_LDST8_DTPREL_LO12";
        case 532: return "R_AARCH64_TLSLD_LDST8_DTPREL_LO12_NC";
        case 533: return "R_AARCH64_TLSLD_LDST16_DTPREL_LO12";
        case 534: return "R_AARCH64_TLSLD_LDST16_DTPREL_LO12_NC";
        case 535: return "R_AARCH64_TLSLD_LDST32_DTPREL_LO12";
        case 536: return "R_AARCH64_TLSLD_LDST32_DTPREL_LO12_NC";
        case 537: return "R_AARCH64_TLSLD_LDST64_DTPREL_LO12";
        case 538: return "R_AARCH64_TLSLD_LDST64_DTPREL_LO12_NC";
        case 539: return "R_AARCH64_TLSIE_MOVW_GOTTPREL_G1";
        case 540: return "R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC";
        case 541: return "R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21";
        case 542: return "R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC";
        case 543: return "R_AARCH64_TLSIE_LD_GOTTPREL_PREL19";
        case 544: return "R_AARCH64_TLSLE_MOVW_TPREL_G2";
        case 545: return "R_AARCH64_TLSLE_MOVW_TPREL_G1";
        case 546: return "R_AARCH64_TLSLE_MOVW_TPREL_G1_NC";
        case 547: return "R_AARCH64_TLSLE_MOVW_TPREL_G0";
        case 548: return "R_AARCH64_TLSLE_MOVW_TPREL_G0_NC";
        case 549: return "R_AARCH64_TLSLE_ADD_TPREL_HI12";
        case 550: return "R_AARCH64_TLSLE_ADD_TPREL_LO12";
        case 551: return "R_AARCH64_TLSLE_ADD_TPREL_LO12_NC";
        case 552: return "R_AARCH64_TLSLE_LDST8_TPREL_LO12";
        case 553: return "R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC";
        case 554: return "R_AARCH64_TLSLE_LDST16_TPREL_LO12";
        case 555: return "R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC";
        case 556: return "R_AARCH64_TLSLE_LDST32_TPREL_LO12";
        case 557: return "R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC";
        case 558: return "R_AARCH64_TLSLE_LDST64_TPREL_LO12";
        case 559: return "R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC";
        default: return NULL;
    }
}

static const char *reloc_type_name(uint16_t machine, uint32_t type) {
    if (machine == EM_386) {
        return reloc_type_i386(type);
    }
    if (machine == EM_X86_64) {
        return reloc_type_x86_64(type);
    }
    if (machine == EM_ARM) {
        return reloc_type_arm(type);
    }
    if (machine == EM_AARCH64) {
        return reloc_type_aarch64(type);
    }
    return NULL;
}

static int symbol_name_value_at(const elf_view_t *view, const elf_header_t *hdr,
                                const section_header_t *symtab, uint32_t symidx,
                                const char **name_out, uint64_t *value_out) {
    section_header_t strsec;
    uint32_t st_name = 0;
    uint8_t st_info = 0;
    uint8_t st_other = 0;
    uint16_t st_shndx = 0;
    uint64_t st_value = 0;
    uint64_t st_size = 0;

    if (symtab == NULL || name_out == NULL || value_out == NULL || symtab->entsize == 0) {
        return -1;
    }
    if ((uint64_t)symidx >= (symtab->size / symtab->entsize)) {
        *name_out = "<corrupt>";
        *value_out = 0;
        return -1;
    }
    if (symtab->link >= shnum_resolved(hdr) || read_shdr(view, hdr, symtab->link, &strsec) != 0 ||
        strsec.off + strsec.size > view->size) {
        *name_out = "<corrupt>";
        *value_out = 0;
        return -1;
    }
    if (view->cls == ELFOBJ_CLASS_64) {
        if (read_sym64(view, symtab->off, symidx, symtab->entsize, &st_name, &st_info, &st_other,
                       &st_shndx, &st_value, &st_size) != 0) {
            *name_out = "<corrupt>";
            *value_out = 0;
            return -1;
        }
    } else {
        if (read_sym32(view, symtab->off, symidx, symtab->entsize, &st_name, &st_info, &st_other,
                       &st_shndx, &st_value, &st_size) != 0) {
            *name_out = "<corrupt>";
            *value_out = 0;
            return -1;
        }
    }
    if (st_name >= strsec.size) {
        *name_out = "<corrupt>";
    } else {
        *name_out = (const char *)(view->data + strsec.off + st_name);
    }
    *value_out = st_value;
    return 0;
}

static void print_relocations(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    uint64_t i;

    for (i = 0; i < shnum; ++i) {
        section_header_t relsec;
        section_header_t symtab;
        uint64_t count;
        uint64_t j;

        if (read_shdr(view, hdr, (size_t)i, &relsec) != 0) {
            break;
        }
        if (relsec.type != SHT_REL && relsec.type != SHT_RELA) {
            continue;
        }
        if (relsec.link >= shnum || read_shdr(view, hdr, relsec.link, &symtab) != 0) {
            warnf("relocation section %" PRIu64 " has invalid symbol table link", i);
            continue;
        }
        if (relsec.entsize == 0 || relsec.off + relsec.size > view->size) {
            warnf("relocation section %" PRIu64 " appears truncated", i);
            continue;
        }

        count = relsec.size / relsec.entsize;
        printf("\nRelocation section at offset 0x%" PRIx64 " contains %" PRIu64 " entries:\n",
               relsec.off, count);
        printf("  Offset          Info           Type           Sym.Value    Sym.Name + Addend\n");

        for (j = 0; j < count; ++j) {
            uint64_t base = relsec.off + (j * relsec.entsize);
            uint64_t r_off = 0;
            uint64_t r_info = 0;
            uint32_t r_type = 0;
            uint32_t r_sym = 0;
            int64_t r_addend = 0;
            const char *rtype_name_local;
            char type_buf[32];
            const char *sym_name = "<none>";
            uint64_t sym_val = 0;

            if (view->cls == ELFOBJ_CLASS_64) {
                if (view_u64(view, (size_t)(base + 0), &r_off) != 0 ||
                    view_u64(view, (size_t)(base + 8), &r_info) != 0) {
                    break;
                }
                r_type = (uint32_t)(r_info & 0xffffffffu);
                r_sym = (uint32_t)(r_info >> 32);
                if (relsec.type == SHT_RELA) {
                    uint64_t add_u = 0;
                    if (view_u64(view, (size_t)(base + 16), &add_u) != 0) {
                        break;
                    }
                    r_addend = (int64_t)add_u;
                }
            } else {
                uint32_t off32 = 0, info32 = 0, add32 = 0;
                if (view_u32(view, (size_t)(base + 0), &off32) != 0 ||
                    view_u32(view, (size_t)(base + 4), &info32) != 0) {
                    break;
                }
                r_off = off32;
                r_info = info32;
                r_type = info32 & 0xffu;
                r_sym = info32 >> 8;
                if (relsec.type == SHT_RELA) {
                    if (view_u32(view, (size_t)(base + 8), &add32) != 0) {
                        break;
                    }
                    r_addend = (int32_t)add32;
                }
            }

            (void)symbol_name_value_at(view, hdr, &symtab, r_sym, &sym_name, &sym_val);
            rtype_name_local = reloc_type_name(hdr->machine, r_type);
            if (rtype_name_local == NULL) {
                snprintf(type_buf, sizeof(type_buf), "%u", r_type);
                rtype_name_local = type_buf;
            }

            if (view->cls == ELFOBJ_CLASS_64) {
                printf("%016" PRIx64 "  %016" PRIx64 " %-14s %016" PRIx64 " %s",
                       r_off, r_info, rtype_name_local, sym_val, sym_name);
            } else {
                printf("%08" PRIx64 "  %08" PRIx64 " %-14s %08" PRIx64 " %s",
                       r_off, r_info, rtype_name_local, sym_val, sym_name);
            }
            if (relsec.type == SHT_RELA) {
                printf(" %+" PRId64, r_addend);
            }
            printf("\n");
        }
    }
}

static const char *dynamic_tag_name(uint64_t tag) {
    switch ((int64_t)tag) {
        case DT_NULL: return "NULL";
        case DT_NEEDED: return "NEEDED";
        case DT_SONAME: return "SONAME";
        case DT_RPATH: return "RPATH";
        case DT_RUNPATH: return "RUNPATH";
        case DT_INIT: return "INIT";
        case DT_FINI: return "FINI";
        case DT_INIT_ARRAY: return "INIT_ARRAY";
        case DT_FINI_ARRAY: return "FINI_ARRAY";
        case DT_HASH: return "HASH";
        case DT_GNU_HASH: return "GNU_HASH";
        case DT_STRTAB: return "STRTAB";
        case DT_SYMTAB: return "SYMTAB";
        case DT_STRSZ: return "STRSZ";
        case DT_SYMENT: return "SYMENT";
        case DT_PLTGOT: return "PLTGOT";
        case DT_PLTRELSZ: return "PLTRELSZ";
        case DT_PLTREL: return "PLTREL";
        case DT_JMPREL: return "JMPREL";
        case DT_REL: return "REL";
        case DT_RELA: return "RELA";
        case DT_RELSZ: return "RELSZ";
        case DT_RELASZ: return "RELASZ";
        case DT_RELENT: return "RELENT";
        case DT_RELAENT: return "RELAENT";
        case DT_TEXTREL: return "TEXTREL";
        case DT_BIND_NOW: return "BIND_NOW";
        case DT_FLAGS: return "FLAGS";
        case DT_FLAGS_1: return "FLAGS_1";
        case DT_VERNEED: return "VERNEED";
        case DT_VERNEEDNUM: return "VERNEEDNUM";
        case DT_VERDEF: return "VERDEF";
        case DT_VERDEFNUM: return "VERDEFNUM";
        case DT_VERSYM: return "VERSYM";
        case DT_DEBUG: return "DEBUG";
        default: return NULL;
    }
}

static void decode_df_flags(uint64_t value, char *buf, size_t buflen) {
    int first = 1;
    buf[0] = '\0';
#define APPEND_FLAG(cond, name) \
    do { \
        if (cond) { \
            snprintf(buf + strlen(buf), buflen - strlen(buf), "%s%s", first ? "" : ",", name); \
            first = 0; \
        } \
    } while (0)
    APPEND_FLAG(value & DF_ORIGIN, "ORIGIN");
    APPEND_FLAG(value & DF_SYMBOLIC, "SYMBOLIC");
    APPEND_FLAG(value & DF_TEXTREL, "TEXTREL");
    APPEND_FLAG(value & DF_BIND_NOW, "BIND_NOW");
    APPEND_FLAG(value & DF_STATIC_TLS, "STATIC_TLS");
#undef APPEND_FLAG
    if (first) {
        snprintf(buf, buflen, "0");
    }
}

static void decode_df1_flags(uint64_t value, char *buf, size_t buflen) {
    int first = 1;
    buf[0] = '\0';
#define APPEND_FLAG(cond, name) \
    do { \
        if (cond) { \
            snprintf(buf + strlen(buf), buflen - strlen(buf), "%s%s", first ? "" : ",", name); \
            first = 0; \
        } \
    } while (0)
    APPEND_FLAG(value & DF_1_NOW, "NOW");
    APPEND_FLAG(value & DF_1_GLOBAL, "GLOBAL");
    APPEND_FLAG(value & DF_1_NODELETE, "NODELETE");
    APPEND_FLAG(value & DF_1_LOADFLTR, "LOADFLTR");
    APPEND_FLAG(value & DF_1_INITFIRST, "INITFIRST");
    APPEND_FLAG(value & DF_1_NOOPEN, "NOOPEN");
    APPEND_FLAG(value & DF_1_ORIGIN, "ORIGIN");
    APPEND_FLAG(value & DF_1_INTERPOSE, "INTERPOSE");
    APPEND_FLAG(value & DF_1_NODEFLIB, "NODEFLIB");
    APPEND_FLAG(value & DF_1_NODUMP, "NODUMP");
    APPEND_FLAG(value & DF_1_PIE, "PIE");
#undef APPEND_FLAG
    if (first) {
        snprintf(buf, buflen, "0");
    }
}

static void print_dynamic(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    uint64_t i;

    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        section_header_t strsec;
        const uint8_t *strtab = NULL;
        size_t strsz = 0;
        uint64_t count;
        uint64_t j;

        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) {
            break;
        }
        if (sh.type != SHT_DYNAMIC || sh.entsize == 0 || sh.off + sh.size > view->size) {
            continue;
        }
        if (sh.link < shnum && read_shdr(view, hdr, sh.link, &strsec) == 0 &&
            strsec.off + strsec.size <= view->size) {
            strtab = view->data + strsec.off;
            strsz = (size_t)strsec.size;
        }

        count = sh.size / sh.entsize;
        printf("\nDynamic section at offset 0x%" PRIx64 " contains %" PRIu64 " entries:\n",
               sh.off, count);
        printf("  Tag                Type                 Name/Value\n");
        for (j = 0; j < count; ++j) {
            uint64_t base = sh.off + (j * sh.entsize);
            uint64_t d_tag_u = 0;
            int64_t d_tag = 0;
            uint64_t d_val = 0;
            const char *name;
            char tagbuf[32];
            char flagbuf[128];

            if (view->cls == ELFOBJ_CLASS_64) {
                if (view_u64(view, (size_t)base, &d_tag_u) != 0 ||
                    view_u64(view, (size_t)(base + 8), &d_val) != 0) {
                    break;
                }
                d_tag = (int64_t)d_tag_u;
            } else {
                uint32_t t32 = 0, v32 = 0;
                if (view_u32(view, (size_t)base, &t32) != 0 ||
                    view_u32(view, (size_t)(base + 4), &v32) != 0) {
                    break;
                }
                d_tag = (int32_t)t32;
                d_val = v32;
            }

            name = dynamic_tag_name((uint64_t)d_tag);
            if (name == NULL) {
                snprintf(tagbuf, sizeof(tagbuf), "<0x%llx>", (unsigned long long)d_tag_u);
                name = tagbuf;
            }

            printf(" 0x%016llx %-20s ", (unsigned long long)d_tag_u, name);
            if (d_tag == DT_NEEDED || d_tag == DT_SONAME || d_tag == DT_RPATH || d_tag == DT_RUNPATH) {
                if (strtab != NULL && d_val < strsz) {
                    printf("[%s]", (const char *)(strtab + d_val));
                } else {
                    printf("<corrupt>");
                }
            } else if (d_tag == DT_FLAGS) {
                decode_df_flags(d_val, flagbuf, sizeof(flagbuf));
                printf("0x%llx (%s)", (unsigned long long)d_val, flagbuf);
            } else if (d_tag == DT_FLAGS_1) {
                decode_df1_flags(d_val, flagbuf, sizeof(flagbuf));
                printf("0x%llx (%s)", (unsigned long long)d_val, flagbuf);
            } else if (d_tag == DT_PLTREL) {
                if (d_val == DT_REL) {
                    printf("REL");
                } else if (d_val == DT_RELA) {
                    printf("RELA");
                } else {
                    printf("0x%llx", (unsigned long long)d_val);
                }
            } else {
                printf("0x%llx", (unsigned long long)d_val);
            }
            printf("\n");
        }
    }
}

static size_t align_up_size(size_t v, size_t align) {
    size_t mask;

    if (align == 0) {
        return v;
    }
    mask = align - 1;
    return (v + mask) & ~mask;
}

static void print_hex_dump(const uint8_t *p, size_t n) {
    size_t i;
    size_t max = n > 32 ? 32 : n;

    for (i = 0; i < max; ++i) {
        printf("%02x%s", p[i], i + 1 == max ? "" : " ");
    }
    if (n > max) {
        printf(" ...");
    }
}

static void decode_gnu_property(uint32_t ptype, const uint8_t *pdata, size_t psz) {
    uint32_t v = 0;

    if (psz >= 4) {
        v = (uint32_t)pdata[0] |
            ((uint32_t)pdata[1] << 8) |
            ((uint32_t)pdata[2] << 16) |
            ((uint32_t)pdata[3] << 24);
    }

    if (ptype == GNU_PROPERTY_STACK_SIZE) {
        printf("stack size = %u", v);
    } else if (ptype == GNU_PROPERTY_NO_COPY_ON_PROTECTED) {
        printf("no-copy-on-protected");
    } else if (ptype == GNU_PROPERTY_X86_ISA_1_USED || ptype == GNU_PROPERTY_X86_ISA_1_NEEDED) {
        printf("%s ", ptype == GNU_PROPERTY_X86_ISA_1_USED ? "x86 ISA used:" : "x86 ISA needed:");
        if (v & 0x1) printf(" BASELINE");
        if (v & 0x2) printf(" V2");
        if (v & 0x4) printf(" V3");
        if (v & 0x8) printf(" V4");
    } else if (ptype == GNU_PROPERTY_X86_FEATURE_1_AND) {
        printf("x86 feature_1_and:");
        if (v & 0x1) printf(" IBT");
        if (v & 0x2) printf(" SHSTK");
    } else if (ptype == GNU_PROPERTY_AARCH64_FEATURE_1_AND) {
        printf("aarch64 feature_1_and:");
        if (v & 0x1) printf(" BTI");
        if (v & 0x2) printf(" PAC");
    } else {
        printf("unknown property (0x%x): ", ptype);
        print_hex_dump(pdata, psz);
    }
}

static void decode_gnu_note(uint32_t type, const uint8_t *desc, size_t descsz, size_t align) {
    if (type == NT_GNU_ABI_TAG && descsz >= 16) {
        uint32_t os = desc[0] | (desc[1] << 8) | (desc[2] << 16) | (desc[3] << 24);
        uint32_t major = desc[4] | (desc[5] << 8) | (desc[6] << 16) | (desc[7] << 24);
        uint32_t minor = desc[8] | (desc[9] << 8) | (desc[10] << 16) | (desc[11] << 24);
        uint32_t sub = desc[12] | (desc[13] << 8) | (desc[14] << 16) | (desc[15] << 24);
        const char *osn = (os == 0) ? "Linux" : (os == 1) ? "GNU" : (os == 2) ? "Solaris" :
                          (os == 3) ? "FreeBSD" : "<unknown>";
        printf("      OS: %s, ABI: %u.%u.%u\n", osn, major, minor, sub);
    } else if (type == NT_GNU_HWCAP) {
        printf("      HWCAP: ");
        print_hex_dump(desc, descsz);
        printf("\n");
    } else if (type == NT_GNU_BUILD_ID) {
        size_t i;
        printf("      Build ID: ");
        for (i = 0; i < descsz; ++i) {
            printf("%02x", desc[i]);
        }
        printf("\n");
    } else if (type == NT_GNU_GOLD_VERSION) {
        printf("      Gold Version: %.*s\n", (int)descsz, (const char *)desc);
    } else if (type == NT_GNU_PROPERTY_TYPE_0) {
        size_t off = 0;
        printf("      GNU Property:\n");
        while (off + 8 <= descsz) {
            uint32_t ptype = desc[off + 0] | (desc[off + 1] << 8) |
                             (desc[off + 2] << 16) | (desc[off + 3] << 24);
            uint32_t psz = desc[off + 4] | (desc[off + 5] << 8) |
                           (desc[off + 6] << 16) | (desc[off + 7] << 24);
            size_t poff = off + 8;
            size_t nnext;
            if (poff + psz > descsz) {
                break;
            }
            printf("        0x%x: ", ptype);
            decode_gnu_property(ptype, desc + poff, psz);
            printf("\n");
            nnext = poff + psz;
            off = align_up_size(nnext, align);
        }
    } else {
        printf("      Unknown GNU note type %u: ", type);
        print_hex_dump(desc, descsz);
        printf("\n");
    }
}

static void parse_note_blob(const elf_view_t *view, uint64_t off, uint64_t size,
                            const char *label, size_t align) {
    size_t cur = 0;

    if (off + size > view->size) {
        return;
    }
    printf("\nDisplaying notes found in: %s\n", label);
    while (cur + 12 <= size) {
        uint32_t namesz = 0;
        uint32_t descsz = 0;
        uint32_t type = 0;
        size_t nh_off = (size_t)off + cur;
        size_t name_off;
        size_t desc_off;
        const uint8_t *name;
        const uint8_t *desc;

        if (view_u32(view, nh_off + 0, &namesz) != 0 ||
            view_u32(view, nh_off + 4, &descsz) != 0 ||
            view_u32(view, nh_off + 8, &type) != 0) {
            break;
        }
        name_off = nh_off + 12;
        desc_off = align_up_size(name_off + namesz, align);
        if (desc_off + descsz > off + size || name_off + namesz > off + size) {
            break;
        }

        name = view->data + name_off;
        desc = view->data + desc_off;
        printf("  Owner                Data size  Type\n");
        printf("  %-20.*s %-10u 0x%x\n", (int)(namesz ? namesz - 1 : 0), (const char *)name, descsz, type);

        if (namesz >= 4 && memcmp(name, "GNU", 3) == 0) {
            decode_gnu_note(type, desc, descsz, align);
        } else {
            printf("      Unknown note payload: ");
            print_hex_dump(desc, descsz);
            printf("\n");
        }

        cur = align_up_size((desc_off + descsz) - off, align);
    }
}

static void print_notes(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    uint64_t i;
    size_t align = (view->cls == ELFOBJ_CLASS_64) ? 8 : 4;

    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) {
            break;
        }
        if (sh.type == SHT_NOTE) {
            parse_note_blob(view, sh.off, sh.size, "SHT_NOTE", align);
        }
    }
    for (i = 0; i < hdr->phnum; ++i) {
        program_header_t ph;
        if (read_phdr(view, hdr, (size_t)i, &ph) != 0) {
            break;
        }
        if (ph.type == PT_NOTE) {
            parse_note_blob(view, ph.off, ph.filesz, "PT_NOTE", align);
        }
    }
}

static const section_header_t *find_section_by_type(const section_header_t *secs, size_t n, uint32_t type) {
    size_t i;
    for (i = 0; i < n; ++i) {
        if (secs[i].type == type) {
            return &secs[i];
        }
    }
    return NULL;
}

static const char *version_name_from_index(const elf_view_t *view, const elf_header_t *hdr,
                                           const section_header_t *secs, size_t nsecs,
                                           uint16_t idx, char *tmp, size_t tmpsz) {
    const section_header_t *verdef = find_section_by_type(secs, nsecs, SHT_GNU_verdef);
    const section_header_t *verneed = find_section_by_type(secs, nsecs, SHT_GNU_verneed);
    const section_header_t *dynstr = NULL;
    elf_view_t sv;
    size_t off;
    (void)hdr;

    if (idx == 0) return "local";
    if (idx == 1) return "global";

    if (verdef != NULL && verdef->link < nsecs) {
        dynstr = &secs[verdef->link];
        if (verdef->off + verdef->size <= view->size && dynstr->off + dynstr->size <= view->size) {
            sv = (elf_view_t){ .data = view->data + verdef->off, .size = (size_t)verdef->size, .endian = view->endian };
            for (off = 0; off + 20 <= sv.size;) {
                uint16_t vd_ndx = 0;
                uint32_t vd_aux = 0;
                uint32_t vd_next = 0;
                uint32_t vda_name = 0;
                if (view_u16(&sv, off + 4, &vd_ndx) != 0 ||
                    view_u32(&sv, off + 12, &vd_aux) != 0 ||
                    view_u32(&sv, off + 16, &vd_next) != 0) break;
                if (vd_ndx == idx && off + vd_aux + 8 <= sv.size &&
                    view_u32(&sv, off + vd_aux, &vda_name) == 0 &&
                    vda_name < dynstr->size) {
                    return (const char *)(view->data + dynstr->off + vda_name);
                }
                if (vd_next == 0) break;
                off += vd_next;
            }
        }
    }

    if (verneed != NULL && verneed->link < nsecs) {
        dynstr = &secs[verneed->link];
        if (verneed->off + verneed->size <= view->size && dynstr->off + dynstr->size <= view->size) {
            sv = (elf_view_t){ .data = view->data + verneed->off, .size = (size_t)verneed->size, .endian = view->endian };
            for (off = 0; off + 16 <= sv.size;) {
                uint16_t vn_cnt = 0;
                uint32_t vn_aux = 0;
                uint32_t vn_next = 0;
                size_t aoff;
                int c;
                if (view_u16(&sv, off + 2, &vn_cnt) != 0 ||
                    view_u32(&sv, off + 8, &vn_aux) != 0 ||
                    view_u32(&sv, off + 12, &vn_next) != 0) break;
                aoff = off + vn_aux;
                for (c = 0; c < vn_cnt && aoff + 16 <= sv.size; ++c) {
                    uint16_t vna_other = 0;
                    uint32_t vna_name = 0;
                    uint32_t vna_next = 0;
                    if (view_u16(&sv, aoff + 6, &vna_other) != 0 ||
                        view_u32(&sv, aoff + 8, &vna_name) != 0 ||
                        view_u32(&sv, aoff + 12, &vna_next) != 0) break;
                    if ((vna_other & 0x7fff) == idx && vna_name < dynstr->size) {
                        return (const char *)(view->data + dynstr->off + vna_name);
                    }
                    if (vna_next == 0) break;
                    aoff += vna_next;
                }
                if (vn_next == 0) break;
                off += vn_next;
            }
        }
    }

    snprintf(tmp, tmpsz, "%u", idx);
    return tmp;
}

static void print_version_info(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    section_header_t *secs;
    uint64_t i;

    secs = (section_header_t *)calloc((size_t)shnum, sizeof(*secs));
    if (secs == NULL) {
        return;
    }
    for (i = 0; i < shnum; ++i) {
        if (read_shdr(view, hdr, (size_t)i, &secs[i]) != 0) {
            break;
        }
    }

    for (i = 0; i < shnum; ++i) {
        if (secs[i].type == SHT_GNU_versym && secs[i].off + secs[i].size <= view->size) {
            elf_view_t sv = {.data = view->data + secs[i].off, .size = (size_t)secs[i].size, .endian = view->endian};
            size_t cnt = sv.size / 2;
            size_t k;
            char tmp[32];
            printf("\nVersion symbols section '.gnu.version' contains %zu entries:\n", cnt);
            for (k = 0; k < cnt; ++k) {
                uint16_t v = 0;
                if (view_u16(&sv, k * 2, &v) != 0) break;
                printf("  %4zu: %u (%s)\n", k, v & 0x7fff,
                       version_name_from_index(view, hdr, secs, (size_t)shnum, (uint16_t)(v & 0x7fff), tmp, sizeof(tmp)));
            }
        } else if (secs[i].type == SHT_GNU_verdef && secs[i].off + secs[i].size <= view->size) {
            elf_view_t sv = {.data = view->data + secs[i].off, .size = (size_t)secs[i].size, .endian = view->endian};
            const section_header_t *dynstr = (secs[i].link < shnum) ? &secs[secs[i].link] : NULL;
            size_t off = 0;
            printf("\nVersion definition section '.gnu.version_d':\n");
            while (off + 20 <= sv.size) {
                uint16_t vd_ndx = 0, vd_flags = 0;
                uint32_t vd_aux = 0, vd_next = 0, vda_name = 0;
                const char *name = "<corrupt>";
                if (view_u16(&sv, off + 2, &vd_flags) != 0 || view_u16(&sv, off + 4, &vd_ndx) != 0 ||
                    view_u32(&sv, off + 12, &vd_aux) != 0 || view_u32(&sv, off + 16, &vd_next) != 0) break;
                if (off + vd_aux + 8 <= sv.size && view_u32(&sv, off + vd_aux, &vda_name) == 0 &&
                    dynstr != NULL && dynstr->off + dynstr->size <= view->size && vda_name < dynstr->size) {
                    name = (const char *)(view->data + dynstr->off + vda_name);
                }
                printf("  Index: %u Flags: 0x%x Name: %s\n", vd_ndx, vd_flags, name);
                if (vd_next == 0) break;
                off += vd_next;
            }
        } else if (secs[i].type == SHT_GNU_verneed && secs[i].off + secs[i].size <= view->size) {
            elf_view_t sv = {.data = view->data + secs[i].off, .size = (size_t)secs[i].size, .endian = view->endian};
            const section_header_t *dynstr = (secs[i].link < shnum) ? &secs[secs[i].link] : NULL;
            size_t off = 0;
            printf("\nVersion needs section '.gnu.version_r':\n");
            while (off + 16 <= sv.size) {
                uint16_t vn_cnt = 0;
                uint32_t vn_file = 0, vn_aux = 0, vn_next = 0;
                const char *file = "<corrupt>";
                size_t aoff;
                int c;
                if (view_u16(&sv, off + 2, &vn_cnt) != 0 || view_u32(&sv, off + 4, &vn_file) != 0 ||
                    view_u32(&sv, off + 8, &vn_aux) != 0 || view_u32(&sv, off + 12, &vn_next) != 0) break;
                if (dynstr != NULL && dynstr->off + dynstr->size <= view->size && vn_file < dynstr->size) {
                    file = (const char *)(view->data + dynstr->off + vn_file);
                }
                printf("  File: %s\n", file);
                aoff = off + vn_aux;
                for (c = 0; c < vn_cnt && aoff + 16 <= sv.size; ++c) {
                    uint16_t vna_flags = 0, vna_other = 0;
                    uint32_t vna_name = 0, vna_next = 0;
                    const char *name = "<corrupt>";
                    if (view_u16(&sv, aoff + 4, &vna_flags) != 0 || view_u16(&sv, aoff + 6, &vna_other) != 0 ||
                        view_u32(&sv, aoff + 8, &vna_name) != 0 || view_u32(&sv, aoff + 12, &vna_next) != 0) break;
                    if (dynstr != NULL && dynstr->off + dynstr->size <= view->size && vna_name < dynstr->size) {
                        name = (const char *)(view->data + dynstr->off + vna_name);
                    }
                    printf("    Name: %s Index: %u Flags: 0x%x\n", name, (unsigned)(vna_other & 0x7fff), vna_flags);
                    if (vna_next == 0) break;
                    aoff += vna_next;
                }
                if (vn_next == 0) break;
                off += vn_next;
            }
        }
    }
    free(secs);
}

static void print_hash_histogram(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    uint64_t i;

    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) {
            break;
        }
        if ((sh.type == SHT_HASH || sh.type == SHT_GNU_HASH) &&
            sh.off + sh.size <= view->size && sh.size >= 8) {
            elf_view_t sv = {.data = view->data + sh.off, .size = (size_t)sh.size, .endian = view->endian};
            uint32_t nbuckets = 0, nchain = 0;
            uint32_t max_chain = 0;
            uint64_t total = 0;
            uint32_t nonempty = 0;
            uint32_t b;

            if (sh.type == SHT_HASH) {
                const uint8_t *base = sv.data;
                const uint32_t *buckets;
                const uint32_t *chains;
                if (view_u32(&sv, 0, &nbuckets) != 0 || view_u32(&sv, 4, &nchain) != 0) {
                    continue;
                }
                if ((size_t)(8 + (nbuckets + nchain) * 4) > sv.size) {
                    continue;
                }
                buckets = (const uint32_t *)(const void *)(base + 8);
                chains = buckets + nbuckets;
                for (b = 0; b < nbuckets; ++b) {
                    uint32_t idx = buckets[b];
                    uint32_t clen = 0;
                    while (idx != 0 && idx < nchain) {
                        ++clen;
                        idx = chains[idx];
                    }
                    if (clen) {
                        ++nonempty;
                        total += clen;
                        if (clen > max_chain) max_chain = clen;
                    }
                }
                printf("\nSYSV hash table: nbucket=%u nchain=%u\n", nbuckets, nchain);
            } else {
                uint32_t symndx = 0, maskwords = 0, shift2 = 0;
                size_t bloom_bytes;
                size_t off;
                if (view_u32(&sv, 0, &nbuckets) != 0 || view_u32(&sv, 4, &symndx) != 0 ||
                    view_u32(&sv, 8, &maskwords) != 0 || view_u32(&sv, 12, &shift2) != 0) {
                    continue;
                }
                bloom_bytes = (size_t)maskwords * (view->cls == ELFOBJ_CLASS_64 ? 8 : 4);
                off = 16 + bloom_bytes;
                if (off + (size_t)nbuckets * 4 > sv.size) {
                    continue;
                }
                {
                    const uint32_t *buckets = (const uint32_t *)(const void *)(sv.data + off);
                    const uint32_t *chains = buckets + nbuckets;
                    size_t chain_off = off + (size_t)nbuckets * 4;
                    for (b = 0; b < nbuckets; ++b) {
                        uint32_t idx = buckets[b];
                        uint32_t clen = 0;
                        if (idx < symndx) {
                            continue;
                        }
                        while (chain_off + (size_t)(idx - symndx) * 4 + 4 <= sv.size) {
                            uint32_t h = chains[idx - symndx];
                            ++clen;
                            if (h & 1u) {
                                break;
                            }
                            ++idx;
                        }
                        if (clen) {
                            ++nonempty;
                            total += clen;
                            if (clen > max_chain) max_chain = clen;
                        }
                    }
                }
                printf("\nGNU hash table: nbucket=%u symndx=%u maskwords=%u shift2=%u bloom_bytes=%zu\n",
                       nbuckets, symndx, maskwords, shift2, bloom_bytes);
            }

            if (nonempty == 0) {
                printf("  no non-empty buckets\n");
            } else {
                printf("  non-empty buckets=%u/%u avg_chain=%.2f max_chain=%u\n",
                       nonempty, nbuckets, (double)total / (double)nonempty, max_chain);
            }
        }
    }
}

static void print_section_groups(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    section_header_t shstr;
    const uint8_t *shstr_data = NULL;
    size_t shstr_size = 0;
    uint64_t i;

    if (hdr->shstrndx < shnum && read_shdr(view, hdr, hdr->shstrndx, &shstr) == 0 &&
        shstr.off + shstr.size <= view->size) {
        shstr_data = view->data + shstr.off;
        shstr_size = (size_t)shstr.size;
    }

    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        elf_view_t gv;
        uint32_t gflags = 0;
        size_t off = 4;

        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) {
            break;
        }
        if (sh.type != SHT_GROUP || sh.off + sh.size > view->size || sh.size < 4) {
            continue;
        }
        gv.data = view->data + sh.off;
        gv.size = (size_t)sh.size;
        gv.endian = view->endian;
        gv.cls = view->cls;

        if (view_u32(&gv, 0, &gflags) != 0) {
            continue;
        }
        printf("\nGroup section [%llu] `%s` [%s]:\n",
               (unsigned long long)i,
               shstr_name(shstr_data, shstr_size, sh.name),
               (gflags & GRP_COMDAT) ? "COMDAT" : "0");
        while (off + 4 <= gv.size) {
            uint32_t member = 0;
            if (view_u32(&gv, off, &member) != 0) {
                break;
            }
            if (member < shnum) {
                section_header_t m;
                if (read_shdr(view, hdr, member, &m) == 0) {
                    printf("  [%u] %s\n", member, shstr_name(shstr_data, shstr_size, m.name));
                } else {
                    printf("  [%u]\n", member);
                }
            } else {
                printf("  [%u] <corrupt>\n", member);
            }
            off += 4;
        }
    }
}

static void decode_unwind_opcode(uint8_t op) {
    if (op == 0xb0) {
        printf("finish");
    } else if ((op & 0xc0) == 0x00) {
        printf("vsp = vsp + %u", ((op & 0x3f) << 2) + 4);
    } else if ((op & 0xf0) == 0x80) {
        printf("pop {r4-r%u}", 4 + (op & 0x0f));
    } else if ((op & 0xf0) == 0x90) {
        printf("vsp = r%u", op & 0x0f);
    } else {
        printf("op=0x%02x", op);
    }
}

static void print_arm_unwind(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    section_header_t shstr;
    const uint8_t *shstr_data = NULL;
    size_t shstr_size = 0;
    uint64_t i;

    if (hdr->machine != EM_ARM) {
        return;
    }

    if (hdr->shstrndx < shnum && read_shdr(view, hdr, hdr->shstrndx, &shstr) == 0 &&
        shstr.off + shstr.size <= view->size) {
        shstr_data = view->data + shstr.off;
        shstr_size = (size_t)shstr.size;
    }

    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        uint64_t n;
        uint64_t j;

        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) {
            break;
        }
        if (strcmp(shstr_name(shstr_data, shstr_size, sh.name), ".ARM.exidx") != 0 ||
            sh.off + sh.size > view->size || sh.size < 8) {
            continue;
        }
        n = sh.size / 8;
        printf("\nARM Exception Index (.ARM.exidx), %llu entries:\n", (unsigned long long)n);
        for (j = 0; j < n; ++j) {
            elf_view_t sv = {.data = view->data + sh.off + j * 8, .size = 8, .endian = view->endian};
            uint32_t prel31 = 0, w1 = 0;
            uint32_t func_addr;
            if (view_u32(&sv, 0, &prel31) != 0 || view_u32(&sv, 4, &w1) != 0) {
                break;
            }
            func_addr = (uint32_t)(sh.addr + j * 8 + (prel31 & 0x7fffffff));
            printf("  [%3llu] function=0x%08x ", (unsigned long long)j, func_addr);
            if (w1 == 1) {
                printf("EXIDX_CANTUNWIND\n");
                continue;
            }
            if (w1 & 0x80000000u) {
                uint8_t b0 = (w1 >> 16) & 0xff;
                uint8_t b1 = (w1 >> 8) & 0xff;
                uint8_t b2 = w1 & 0xff;
                printf("compact:");
                printf(" [");
                decode_unwind_opcode(b0);
                printf("] [");
                decode_unwind_opcode(b1);
                printf("] [");
                decode_unwind_opcode(b2);
                printf("]\n");
            } else {
                printf("extab=0x%08x\n", w1);
            }
        }
        printf("  .ARM.extab cross-reference: ");
        {
            uint64_t k;
            int found = 0;
            for (k = 0; k < shnum; ++k) {
                section_header_t x;
                if (read_shdr(view, hdr, (size_t)k, &x) != 0) break;
                if (strcmp(shstr_name(shstr_data, shstr_size, x.name), ".ARM.extab") == 0) {
                    found = 1;
                    printf("present at offset 0x%llx", (unsigned long long)x.off);
                    break;
                }
            }
            if (!found) {
                printf("not present");
            }
            printf("\n");
        }
    }
}

static int read_uleb(const uint8_t *buf, size_t size, size_t *off, uint64_t *out) {
    uint64_t v = 0;
    unsigned shift = 0;
    size_t i = *off;

    while (i < size) {
        uint8_t b = buf[i++];
        v |= (uint64_t)(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            *off = i;
            *out = v;
            return 0;
        }
        shift += 7;
        if (shift > 63) {
            return -1;
        }
    }
    return -1;
}

static const char *arm_attr_tag_name(uint64_t tag) {
    switch (tag) {
        case 4: return "Tag_CPU_name";
        case 6: return "Tag_CPU_arch";
        case 7: return "Tag_CPU_arch_profile";
        case 8: return "Tag_ARM_ISA_use";
        case 9: return "Tag_THUMB_ISA_use";
        case 10: return "Tag_FP_arch";
        case 11: return "Tag_WMMX_arch";
        case 12: return "Tag_Advanced_SIMD_arch";
        case 13: return "Tag_ABI_PCS_config";
        case 14: return "Tag_ABI_PCS_R9_use";
        case 15: return "Tag_ABI_PCS_RW_data";
        case 16: return "Tag_ABI_PCS_RO_data";
        case 17: return "Tag_ABI_PCS_GOT_use";
        case 18: return "Tag_ABI_PCS_wchar_t";
        case 19: return "Tag_ABI_FP_rounding";
        case 20: return "Tag_ABI_FP_denormal";
        case 21: return "Tag_ABI_FP_exceptions";
        case 22: return "Tag_ABI_FP_user_exceptions";
        case 23: return "Tag_ABI_FP_number_model";
        case 24: return "Tag_ABI_align_needed";
        case 25: return "Tag_ABI_align_preserved";
        case 26: return "Tag_ABI_enum_size";
        case 27: return "Tag_ABI_HardFP_use";
        case 28: return "Tag_ABI_VFP_args";
        case 34: return "Tag_CPU_unaligned_access";
        case 36: return "Tag_FP_HP_extension";
        case 42: return "Tag_MPExtension_use";
        case 44: return "Tag_DIV_use";
        case 46: return "Tag_DSP_extension";
        case 68: return "Tag_Virtualization_use";
        default: return NULL;
    }
}

static const char *arm_cpu_arch_name(uint64_t v) {
    switch (v) {
        case 0: return "pre-v4";
        case 1: return "v4";
        case 2: return "v4T";
        case 3: return "v5T";
        case 4: return "v5TE";
        case 5: return "v5TEJ";
        case 6: return "v6";
        case 7: return "v6KZ";
        case 8: return "v6T2";
        case 9: return "v6K";
        case 10: return "v7";
        case 11: return "v6-M";
        case 12: return "v6S-M";
        case 13: return "v7E-M";
        case 14: return "v8";
        default: return NULL;
    }
}

static void print_arm_attributes(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t shnum = shnum_resolved(hdr);
    uint64_t i;

    (void)hdr;
    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        const uint8_t *buf;
        size_t size;
        size_t off;

        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) break;
        if (sh.type != SHT_ARM_ATTRIBUTES || sh.off + sh.size > view->size || sh.size < 6) continue;

        buf = view->data + sh.off;
        size = (size_t)sh.size;
        if (buf[0] != 'A') {
            continue;
        }
        printf("\nARM Attributes section:\n");
        off = 1;
        while (off + 4 <= size) {
            uint32_t sect_len = (uint32_t)buf[off] |
                                ((uint32_t)buf[off + 1] << 8) |
                                ((uint32_t)buf[off + 2] << 16) |
                                ((uint32_t)buf[off + 3] << 24);
            size_t vend_off = off + 4;
            size_t vend_end = vend_off;
            size_t sub_off;
            if (sect_len == 0 || off + sect_len > size) break;
            while (vend_end < off + sect_len && buf[vend_end] != '\0') {
                ++vend_end;
            }
            if (vend_end >= off + sect_len) break;
            printf("  Vendor: %s\n", (const char *)(buf + vend_off));
            if (strcmp((const char *)(buf + vend_off), "aeabi") != 0) {
                printf("    (unknown vendor subsection, raw parse skipped)\n");
                off += sect_len;
                continue;
            }
            sub_off = vend_end + 1;
            while (sub_off + 5 <= off + sect_len) {
                uint8_t tag = buf[sub_off];
                uint32_t sub_len = (uint32_t)buf[sub_off + 1] |
                                   ((uint32_t)buf[sub_off + 2] << 8) |
                                   ((uint32_t)buf[sub_off + 3] << 16) |
                                   ((uint32_t)buf[sub_off + 4] << 24);
                size_t p = sub_off + 5;
                if (sub_len < 5 || sub_off + sub_len > off + sect_len) break;
                printf("    Subsection tag %u\n", tag);
                while (p < sub_off + sub_len) {
                    uint64_t atag = 0;
                    uint64_t aval = 0;
                    const char *name;
                    if (read_uleb(buf, sub_off + sub_len, &p, &atag) != 0) break;
                    name = arm_attr_tag_name(atag);
                    if (atag == 4) {
                        const char *s = (const char *)(buf + p);
                        size_t sl = strlen(s);
                        printf("      %s: %s\n", name ? name : "Tag", s);
                        p += sl + 1;
                    } else {
                        const char *desc = NULL;
                        if (read_uleb(buf, sub_off + sub_len, &p, &aval) != 0) break;
                        if (atag == 6) {
                            desc = arm_cpu_arch_name(aval);
                        }
                        if (desc != NULL) {
                            printf("      %s: %s (%llu)\n", name ? name : "Tag", desc,
                                   (unsigned long long)aval);
                        } else {
                            printf("      %s: %llu\n", name ? name : "Tag",
                                   (unsigned long long)aval);
                        }
                    }
                }
                sub_off += sub_len;
            }
            off += sect_len;
        }
    }
}

static int parse_u64_text(const char *s, uint64_t *out) {
    char *end = NULL;
    unsigned long long v;
    if (s == NULL || *s == '\0') return -1;
    errno = 0;
    v = strtoull(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static int resolve_section_target(const elf_view_t *view, const elf_header_t *hdr,
                                  const char *target, section_header_t *out, uint64_t *idx_out) {
    uint64_t idx = 0;
    uint64_t shnum = shnum_resolved(hdr);
    section_header_t shstr;
    const uint8_t *shstr_data = NULL;
    size_t shstr_size = 0;
    uint64_t i;

    if (parse_u64_text(target, &idx) == 0) {
        if (idx >= shnum || read_shdr(view, hdr, (size_t)idx, out) != 0) {
            return -1;
        }
        *idx_out = idx;
        return 0;
    }

    if (hdr->shstrndx < shnum && read_shdr(view, hdr, hdr->shstrndx, &shstr) == 0 &&
        shstr.off + shstr.size <= view->size) {
        shstr_data = view->data + shstr.off;
        shstr_size = (size_t)shstr.size;
    }
    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        const char *nm;
        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) break;
        nm = shstr_name(shstr_data, shstr_size, sh.name);
        if (strcmp(nm, target) == 0) {
            *out = sh;
            *idx_out = i;
            return 0;
        }
    }
    return -1;
}

static void do_hex_dump(const elf_view_t *view, const elf_header_t *hdr, const char *target) {
    section_header_t sh;
    uint64_t idx = 0;
    uint64_t off = 0;
    if (target == NULL) return;
    if (resolve_section_target(view, hdr, target, &sh, &idx) != 0) {
        warnf("section target '%s' not found", target);
        return;
    }
    if (sh.off + sh.size > view->size) {
        warnf("section '%s' is truncated", target);
        return;
    }
    printf("\nHex dump of section [%llu]:\n", (unsigned long long)idx);
    while (off < sh.size) {
        uint64_t line = (sh.size - off > 16) ? 16 : (sh.size - off);
        uint64_t i;
        printf("  0x%08llx ", (unsigned long long)off);
        for (i = 0; i < line; ++i) {
            printf("%02x ", view->data[sh.off + off + i]);
        }
        printf("\n");
        off += line;
    }
}

static void do_string_dump(const elf_view_t *view, const elf_header_t *hdr, const char *target) {
    section_header_t sh;
    uint64_t idx = 0;
    uint64_t off = 0;
    if (target == NULL) return;
    if (resolve_section_target(view, hdr, target, &sh, &idx) != 0) {
        warnf("section target '%s' not found", target);
        return;
    }
    if (sh.off + sh.size > view->size) {
        warnf("section '%s' is truncated", target);
        return;
    }
    printf("\nString dump of section [%llu]:\n", (unsigned long long)idx);
    while (off < sh.size) {
        const uint8_t *p = view->data + sh.off + off;
        size_t len = 0;
        while (off + len < sh.size && p[len] != '\0') {
            ++len;
        }
        if (len >= 1) {
            printf("  [%6llu] %.*s\n", (unsigned long long)off, (int)len, (const char *)p);
        }
        off += len + 1;
    }
}

static int debug_kind_match(const char *kind, const char *name) {
    if (kind == NULL || strcmp(kind, "all") == 0) return 1;
    if (strcmp(kind, "info") == 0) return strcmp(name, ".debug_info") == 0 || strcmp(name, ".zdebug_info") == 0;
    if (strcmp(kind, "abbrev") == 0) return strcmp(name, ".debug_abbrev") == 0 || strcmp(name, ".zdebug_abbrev") == 0;
    if (strcmp(kind, "line") == 0) return strcmp(name, ".debug_line") == 0 || strcmp(name, ".zdebug_line") == 0;
    if (strcmp(kind, "frames") == 0) return strcmp(name, ".debug_frame") == 0 || strcmp(name, ".eh_frame") == 0;
    if (strcmp(kind, "ranges") == 0) return strcmp(name, ".debug_ranges") == 0 || strcmp(name, ".debug_rnglists") == 0;
    if (strcmp(kind, "str") == 0) return strcmp(name, ".debug_str") == 0 || strcmp(name, ".zdebug_str") == 0;
    if (strcmp(kind, "aranges") == 0) return strcmp(name, ".debug_aranges") == 0;
    if (strcmp(kind, "loc") == 0) return strcmp(name, ".debug_loc") == 0 || strcmp(name, ".debug_loclists") == 0;
    return 0;
}

static void print_debug_info_units(const elf_view_t *view, const section_header_t *sec) {
    elf_view_t sv = {.data = view->data + sec->off, .size = (size_t)sec->size, .endian = view->endian};
    size_t off = 0;
    printf("  .debug_info units:\n");
    while (off + 11 <= sv.size) {
        uint32_t len32 = 0;
        uint16_t ver = 0;
        uint8_t addr_size = 0;
        uint64_t unit_len;
        size_t hdrsz;
        if (view_u32(&sv, off, &len32) != 0) break;
        if (len32 == 0xffffffffu) {
            uint64_t len64 = 0;
            if (off + 23 > sv.size || view_u64(&sv, off + 4, &len64) != 0) break;
            unit_len = len64;
            hdrsz = 12;
            if (view_u16(&sv, off + 12, &ver) != 0) break;
            addr_size = sv.data[off + 22];
        } else {
            unit_len = len32;
            hdrsz = 4;
            if (view_u16(&sv, off + 4, &ver) != 0) break;
            if (off + 11 > sv.size) break;
            addr_size = sv.data[off + 10];
        }
        printf("    offset=0x%zx version=%u addr_size=%u len=%llu\n",
               off, ver, addr_size, (unsigned long long)unit_len);
        off += hdrsz + (size_t)unit_len;
    }
}

static void print_debug_dump(const elf_view_t *view, const elf_header_t *hdr, const char *kind) {
    uint64_t shnum = shnum_resolved(hdr);
    section_header_t shstr;
    const uint8_t *shstr_data = NULL;
    size_t shstr_size = 0;
    uint64_t i;

    if (hdr->shstrndx < shnum && read_shdr(view, hdr, hdr->shstrndx, &shstr) == 0 &&
        shstr.off + shstr.size <= view->size) {
        shstr_data = view->data + shstr.off;
        shstr_size = (size_t)shstr.size;
    }

    for (i = 0; i < shnum; ++i) {
        section_header_t sh;
        const char *name;
        if (read_shdr(view, hdr, (size_t)i, &sh) != 0) break;
        name = shstr_name(shstr_data, shstr_size, sh.name);
        if (!debug_kind_match(kind, name)) continue;
        if (sh.off + sh.size > view->size) {
            warnf("debug section %s is truncated", name);
            continue;
        }
        printf("\nDWARF section %s size=0x%llx", name, (unsigned long long)sh.size);
        if ((sh.flags & SHF_COMPRESSED) != 0 || strncmp(name, ".zdebug_", 8) == 0) {
            printf(" [compressed]");
        }
        printf("\n");
        if (strcmp(name, ".debug_info") == 0 || strcmp(name, ".zdebug_info") == 0) {
            print_debug_info_units(view, &sh);
        } else {
            do_hex_dump(view, hdr, name);
        }
    }
}

static void decode_core_note(uint32_t type, const uint8_t *desc, size_t descsz, const elf_view_t *view) {
    (void)view;
    if (type == NT_PRSTATUS) {
        int signo = 0;
        if (descsz >= 4) {
            signo = (int)(desc[0] | (desc[1] << 8) | (desc[2] << 16) | (desc[3] << 24));
        }
        printf("    NT_PRSTATUS signal=%d regs=%zu bytes\n", signo, descsz);
    } else if (type == NT_PRPSINFO) {
        printf("    NT_PRPSINFO size=%zu\n", descsz);
    } else if (type == NT_FPREGSET) {
        printf("    NT_FPREGSET size=%zu\n", descsz);
    } else if (type == NT_AUXV) {
        printf("    NT_AUXV entries (raw) size=%zu\n", descsz);
    } else if (type == NT_FILE) {
        printf("    NT_FILE mappings:\n");
        if (descsz >= (view->cls == ELFOBJ_CLASS_64 ? 16 : 8)) {
            uint64_t count = 0, page = 0;
            size_t off = 0;
            if (view->cls == ELFOBJ_CLASS_64) {
                count = *(const uint64_t *)(const void *)(desc + 0);
                page = *(const uint64_t *)(const void *)(desc + 8);
                off = 16;
            } else {
                count = *(const uint32_t *)(const void *)(desc + 0);
                page = *(const uint32_t *)(const void *)(desc + 4);
                off = 8;
            }
            printf("      count=%llu page_size=%llu\n",
                   (unsigned long long)count, (unsigned long long)page);
            if (view->cls == ELFOBJ_CLASS_64 && descsz >= off + count * 24) {
                uint64_t i;
                const uint8_t *p = desc + off;
                const char *names = (const char *)(desc + off + count * 24);
                for (i = 0; i < count; ++i) {
                    uint64_t start = ((const uint64_t *)p)[0];
                    uint64_t end = ((const uint64_t *)p)[1];
                    uint64_t foff = ((const uint64_t *)p)[2];
                    printf("      0x%llx-0x%llx @0x%llx %s\n",
                           (unsigned long long)start, (unsigned long long)end,
                           (unsigned long long)foff, names);
                    names += strlen(names) + 1;
                    p += 24;
                }
            }
        }
    } else {
        printf("    note type=0x%x size=%zu\n", type, descsz);
    }
}

static void print_core_info(const elf_view_t *view, const elf_header_t *hdr) {
    uint64_t i;
    size_t align = (view->cls == ELFOBJ_CLASS_64) ? 8 : 4;

    if (hdr->type != ET_CORE) {
        return;
    }
    printf("\nCore file notes:\n");
    for (i = 0; i < hdr->phnum; ++i) {
        program_header_t ph;
        size_t cur = 0;
        if (read_phdr(view, hdr, (size_t)i, &ph) != 0) break;
        if (ph.type != PT_NOTE || ph.off + ph.filesz > view->size) continue;
        while (cur + 12 <= ph.filesz) {
            elf_view_t sv = {.data = view->data + ph.off + cur, .size = (size_t)(ph.filesz - cur), .endian = view->endian};
            uint32_t namesz = 0, descsz = 0, type = 0;
            size_t name_off, desc_off;
            const uint8_t *desc;
            if (view_u32(&sv, 0, &namesz) != 0 || view_u32(&sv, 4, &descsz) != 0 || view_u32(&sv, 8, &type) != 0) break;
            name_off = 12;
            desc_off = align_up_size(name_off + namesz, align);
            if (desc_off + descsz > sv.size) break;
            desc = sv.data + desc_off;
            decode_core_note(type, desc, descsz, view);
            cur += align_up_size(desc_off + descsz, align);
        }
    }
}

static int read_header(const elf_view_t *view, elf_header_t *out) {
    size_t need;

    if (view == NULL || out == NULL || view->size < EI_NIDENT) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->ident, view->data, EI_NIDENT);

    if (view->cls == ELFOBJ_CLASS_64) {
        need = 64;
        if (view->size < need) {
            out->truncated = 1;
        }
        if (view_u16(view, 16, &out->type) != 0) return -1;
        if (view_u16(view, 18, &out->machine) != 0) return -1;
        if (view_u32(view, 20, &out->version) != 0) return -1;
        if (view_u64(view, 24, &out->entry) != 0) return -1;
        if (view_u64(view, 32, &out->phoff) != 0) return -1;
        if (view_u64(view, 40, &out->shoff) != 0) return -1;
        if (view_u32(view, 48, &out->flags) != 0) return -1;
        if (view_u16(view, 52, &out->ehsize) != 0) return -1;
        if (view_u16(view, 54, &out->phentsize) != 0) return -1;
        if (view_u16(view, 56, &out->phnum) != 0) return -1;
        if (view_u16(view, 58, &out->shentsize) != 0) return -1;
        if (view_u16(view, 60, &out->shnum) != 0) return -1;
        if (view_u16(view, 62, &out->shstrndx) != 0) return -1;
    } else if (view->cls == ELFOBJ_CLASS_32) {
        need = 52;
        if (view->size < need) {
            out->truncated = 1;
        }
        if (view_u16(view, 16, &out->type) != 0) return -1;
        if (view_u16(view, 18, &out->machine) != 0) return -1;
        if (view_u32(view, 20, &out->version) != 0) return -1;
        {
            uint32_t v = 0;
            if (view_u32(view, 24, &v) != 0) return -1;
            out->entry = v;
            if (view_u32(view, 28, &v) != 0) return -1;
            out->phoff = v;
            if (view_u32(view, 32, &v) != 0) return -1;
            out->shoff = v;
            if (view_u32(view, 36, &v) != 0) return -1;
            out->flags = v;
        }
        if (view_u16(view, 40, &out->ehsize) != 0) return -1;
        if (view_u16(view, 42, &out->phentsize) != 0) return -1;
        if (view_u16(view, 44, &out->phnum) != 0) return -1;
        if (view_u16(view, 46, &out->shentsize) != 0) return -1;
        if (view_u16(view, 48, &out->shnum) != 0) return -1;
        if (view_u16(view, 50, &out->shstrndx) != 0) return -1;
    } else {
        return -1;
    }
    return 0;
}

static void print_arm_flags(uint32_t flags) {
    uint32_t eabi = flags & EF_ARM_EABIMASK;
    int printed = 0;

    printf("0x%08x", flags);
    if (flags == 0) {
        printf("\n");
        return;
    }
    printf(", ");

    if (eabi == EF_ARM_EABI_VER5) {
        printf("Version5 EABI");
        printed = 1;
    } else if (eabi == EF_ARM_EABI_VER4) {
        printf("Version4 EABI");
        printed = 1;
    } else if (eabi == EF_ARM_EABI_VER3) {
        printf("Version3 EABI");
        printed = 1;
    } else if (eabi == EF_ARM_EABI_VER2) {
        printf("Version2 EABI");
        printed = 1;
    } else if (eabi == EF_ARM_EABI_VER1) {
        printf("Version1 EABI");
        printed = 1;
    }

    if (flags & EF_ARM_VFP_FLOAT) {
        printf("%shard-float ABI", printed ? ", " : "");
        printed = 1;
    }
    if (flags & EF_ARM_SOFT_FLOAT) {
        printf("%ssoft-float ABI", printed ? ", " : "");
        printed = 1;
    }
    if (flags & EF_ARM_BE8) {
        printf("%sBE8", printed ? ", " : "");
        printed = 1;
    }
    if (flags & EF_ARM_INTERWORK) {
        printf("%sinterworking enabled", printed ? ", " : "");
        printed = 1;
    }
    if (!printed) {
        printf("unknown ARM flags");
    }
    printf("\n");
}

static void print_file_header(const char *path, const elf_view_t *view, const elf_header_t *hdr) {
    const char *type;
    const char *mach;
    const char *abi;

    printf("ELF Header:\n");
    printf("  Magic:  ");
    for (size_t i = 0; i < EI_NIDENT; ++i) {
        printf("%02x%s", hdr->ident[i], i + 1 == EI_NIDENT ? "" : " ");
    }
    printf("\n");

    printf("  Class:                             %s\n",
           view->cls == ELFOBJ_CLASS_64 ? "ELF64" :
           (view->cls == ELFOBJ_CLASS_32 ? "ELF32" : "<unknown>"));
    printf("  Data:                              %s\n",
           hdr->ident[EI_DATA] == ELFDATA2MSB ? "2's complement, big endian" :
           "2's complement, little endian");
    printf("  Version:                           %u\n", (unsigned)hdr->ident[6]);
    abi = osabi_name(hdr->ident[7]);
    if (abi != NULL) {
        printf("  OS/ABI:                            %s\n", abi);
    } else {
        printf("  OS/ABI:                            <unknown: %u>\n", (unsigned)hdr->ident[7]);
    }
    printf("  ABI Version:                       %u\n", (unsigned)hdr->ident[8]);

    type = type_name(hdr->type);
    if (type != NULL) {
        printf("  Type:                              %s\n", type);
    } else {
        printf("  Type:                              <unknown>: 0x%x\n", hdr->type);
    }
    mach = machine_name(hdr->machine);
    if (mach != NULL) {
        printf("  Machine:                           %s\n", mach);
    } else {
        printf("  Machine:                           <unknown>: 0x%x\n", hdr->machine);
    }
    printf("  Version:                           0x%x\n", hdr->version);
    printf("  Entry point address:               0x%" PRIx64 "\n", hdr->entry);
    printf("  Start of program headers:          %" PRIu64 " (bytes into file)\n", hdr->phoff);
    printf("  Start of section headers:          %" PRIu64 " (bytes into file)\n", hdr->shoff);
    printf("  Flags:                             ");
    if (hdr->machine == EM_ARM) {
        print_arm_flags(hdr->flags);
    } else {
        printf("0x%08x\n", hdr->flags);
    }
    printf("  Size of this header:               %u (bytes)\n", hdr->ehsize);
    printf("  Size of program headers:           %u (bytes)\n", hdr->phentsize);
    printf("  Number of program headers:         %u\n", hdr->phnum);
    printf("  Size of section headers:           %u (bytes)\n", hdr->shentsize);
    printf("  Number of section headers:         %u\n", hdr->shnum);
    printf("  Section header string table index: %u\n", hdr->shstrndx);
    if (hdr->truncated) {
        warnf("'%s': ELF header appears truncated", path);
    }
}

static void warnf(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "%s: Error: ", g_progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void usage(FILE *out) {
    fprintf(out,
            "usage: %s [options] <file>...\n"
            "  -h, --file-header       Display the ELF file header\n"
            "  -l, --program-headers   Display program headers\n"
            "  -S, --section-headers   Display section headers\n"
            "  -s, --syms,--symbols    Display symbol table entries\n"
            "  -r, --relocs            Display relocations\n"
            "  -d, --dynamic           Display dynamic section\n"
            "  -n, --notes             Display notes\n"
            "  -V, --version-info      Display version sections\n"
            "  -I, --histogram         Display hash histogram\n"
            "  -g, --section-groups    Display section groups\n"
            "  -u, --unwind            Display ARM unwind tables\n"
            "  -A, --arch-specific     Display arch-specific attributes\n"
            "  -x, --hex-dump=SEC      Hex dump section by name or index\n"
            "  -p, --string-dump=SEC   String dump section by name or index\n"
            "  -t, --section-details   Show extended section details\n"
            "  -C, --demangle          Demangle symbol names\n"
            "  -D, --use-dynamic       Use dynamic symbol table\n"
            "      --sym-base=0|8|10|16  Symbol value radix\n"
            "      --print-sysv        SysV style output where supported\n"
            "  -w, --debug-dump=KIND   Dump DWARF info (info,abbrev,line,frames,ranges,str,aranges,loc)\n"
            "  -c                      Display core-file notes\n"
            "  -e, --headers           Equivalent to -h -l -S\n"
            "  -a, --all               Display all core information\n"
            "  -W, --wide              Do not limit output width\n"
            "      --dyn-syms          Display dynamic symbols only\n"
            "      --help              Display this help and exit\n"
            "      --version           Display version information and exit\n",
            g_progname);
}

static void print_version(void) {
    printf("%s %s\n", g_progname, READELF_VERSION);
}

static int parse_options(int argc, char **argv, readelf_opts_t *opts) {
    static const struct option longopts[] = {
        {"file-header", no_argument, NULL, 'h'},
        {"program-headers", no_argument, NULL, 'l'},
        {"segments", no_argument, NULL, 'l'},
        {"section-headers", no_argument, NULL, 'S'},
        {"sections", no_argument, NULL, 'S'},
        {"syms", no_argument, NULL, 's'},
        {"symbols", no_argument, NULL, 's'},
        {"relocs", no_argument, NULL, 'r'},
        {"dynamic", no_argument, NULL, 'd'},
        {"notes", no_argument, NULL, 'n'},
        {"version-info", no_argument, NULL, 'V'},
        {"histogram", no_argument, NULL, 'I'},
        {"section-groups", no_argument, NULL, 'g'},
        {"unwind", no_argument, NULL, 'u'},
        {"arch-specific", no_argument, NULL, 'A'},
        {"hex-dump", required_argument, NULL, 'x'},
        {"string-dump", required_argument, NULL, 'p'},
        {"section-details", no_argument, NULL, 't'},
        {"demangle", no_argument, NULL, 'C'},
        {"use-dynamic", no_argument, NULL, 'D'},
        {"sym-base", required_argument, NULL, 4},
        {"print-sysv", no_argument, NULL, 5},
        {"debug-dump", required_argument, NULL, 'w'},
        {"headers", no_argument, NULL, 'e'},
        {"all", no_argument, NULL, 'a'},
        {"wide", no_argument, NULL, 'W'},
        {"dyn-syms", no_argument, NULL, 3},
        {"help", no_argument, NULL, 1},
        {"version", no_argument, NULL, 2},
        {0, 0, 0, 0},
    };
    int ch;

    if (opts == NULL) {
        return -1;
    }
    memset(opts, 0, sizeof(*opts));

    optind = 1;
    opts->sym_base = 16;
    while ((ch = getopt_long(argc, argv, "hlsrdnVIguAx:p:tCDw:cSeaW", longopts, NULL)) != -1) {
        switch (ch) {
            case 'h':
                opts->show_file_header = 1;
                break;
            case 'l':
                opts->show_program_headers = 1;
                break;
            case 'S':
                opts->show_section_headers = 1;
                break;
            case 's':
                opts->show_symbols = 1;
                break;
            case 'r':
                opts->show_relocs = 1;
                break;
            case 'd':
                opts->show_dynamic = 1;
                break;
            case 'n':
                opts->show_notes = 1;
                break;
            case 'V':
                opts->show_version_info = 1;
                break;
            case 'I':
                opts->show_histogram = 1;
                break;
            case 'g':
                opts->show_groups = 1;
                break;
            case 'u':
                opts->show_unwind = 1;
                break;
            case 'A':
                opts->show_arch_specific = 1;
                break;
            case 'x':
                opts->hex_dump_target = optarg;
                break;
            case 'p':
                opts->str_dump_target = optarg;
                break;
            case 't':
                opts->section_details = 1;
                opts->show_section_headers = 1;
                break;
            case 'C':
                opts->demangle_names = 1;
                break;
            case 'D':
                opts->use_dynamic_for_symbols = 1;
                opts->show_symbols = 1;
                break;
            case 'w':
                opts->show_debug_dump = 1;
                opts->debug_dump_kind = optarg;
                break;
            case 'c':
                opts->show_core = 1;
                break;
            case 'e':
                opts->show_file_header = 1;
                opts->show_program_headers = 1;
                opts->show_section_headers = 1;
                break;
            case 'a':
                opts->show_file_header = 1;
                opts->show_program_headers = 1;
                opts->show_section_headers = 1;
                opts->show_symbols = 1;
                opts->show_relocs = 1;
                opts->show_dynamic = 1;
                opts->show_notes = 1;
                opts->show_version_info = 1;
                opts->show_histogram = 1;
                opts->show_groups = 1;
                opts->show_unwind = 1;
                opts->show_arch_specific = 1;
                opts->show_core = 1;
                break;
            case 'W':
                opts->wide = 1;
                break;
            case 1:
                usage(stdout);
                return 1;
            case 2:
                print_version();
                return 1;
            case 3:
                opts->show_symbols = 1;
                opts->only_dynsyms = 1;
                break;
            case 4:
                if (strcmp(optarg, "0") == 0 || strcmp(optarg, "8") == 0 ||
                    strcmp(optarg, "10") == 0 || strcmp(optarg, "16") == 0) {
                    opts->sym_base = atoi(optarg);
                } else {
                    warnf("invalid --sym-base value '%s'", optarg);
                    return -1;
                }
                break;
            case 5:
                opts->print_sysv = 1;
                break;
            default:
                usage(stderr);
                return -1;
        }
    }
    return 0;
}

static int open_elf(const char *path, elfobj_t **out_obj) {
    elf_err_t err;

    if (path == NULL || out_obj == NULL) {
        return -1;
    }
    *out_obj = NULL;
    err = elf_open(path, out_obj);
    if (err != ELF_OK || *out_obj == NULL) {
        if (err == ELF_ERR_FORMAT) {
            warnf("'%s': Not an ELF file", path);
        } else {
            warnf("'%s': %s", path, elf_errstr(err));
        }
        return -1;
    }
    return 0;
}

static int process_file(const char *path, const readelf_opts_t *opts, int multiple_files) {
    elfobj_t *obj = NULL;
    int rc = 0;
    elf_view_t view;
    elf_header_t hdr;
    FILE *fp = NULL;
    uint8_t *buf = NULL;
    long fsz;

    if (path == NULL) {
        return -1;
    }
    if (open_elf(path, &obj) != 0) {
        return -1;
    }

    if (multiple_files) {
        printf("\nFile: %s\n", path);
    }

    if (!opts->show_file_header &&
        !opts->show_program_headers &&
        !opts->show_section_headers &&
        !opts->show_symbols &&
        !opts->show_relocs &&
        !opts->show_dynamic &&
        !opts->show_notes &&
        !opts->show_version_info &&
        !opts->show_histogram &&
        !opts->show_groups &&
        !opts->show_unwind &&
        !opts->show_arch_specific &&
        opts->hex_dump_target == NULL &&
        opts->str_dump_target == NULL &&
        !opts->show_debug_dump &&
        !opts->show_core) {
        opts = &(readelf_opts_t){
            .show_file_header = 1,
        };
    }

    memset(&view, 0, sizeof(view));
    view.cls = elf_class(obj);
    view.endian = elf_endian(obj);

    fp = fopen(path, "rb");
    if (fp == NULL) {
        warnf("'%s': %s", path, strerror(errno));
        rc = 1;
        goto out;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        warnf("'%s': failed to seek", path);
        rc = 1;
        goto out;
    }
    fsz = ftell(fp);
    if (fsz < 0) {
        warnf("'%s': failed to determine size", path);
        rc = 1;
        goto out;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        warnf("'%s': failed to rewind", path);
        rc = 1;
        goto out;
    }
    buf = (uint8_t *)malloc((size_t)fsz);
    if (buf == NULL && fsz != 0) {
        warnf("'%s': out of memory", path);
        rc = 1;
        goto out;
    }
    if (fsz > 0 && fread(buf, 1, (size_t)fsz, fp) != (size_t)fsz) {
        warnf("'%s': failed to read file", path);
        rc = 1;
        goto out;
    }
    view.data = buf;
    view.size = (size_t)fsz;

    memset(&hdr, 0, sizeof(hdr));
    if (read_header(&view, &hdr) != 0) {
        warnf("'%s': failed to parse ELF header", path);
        rc = 1;
        goto out;
    }

    if (opts->show_file_header) {
        print_file_header(path, &view, &hdr);
    }
    if (opts->show_section_headers) {
        print_section_headers(opts, &view, &hdr);
    }
    if (opts->show_program_headers) {
        print_program_headers(&view, &hdr);
    }
    if (opts->show_symbols) {
        print_symbol_tables(opts, &view, &hdr);
    }
    if (opts->show_relocs) {
        print_relocations(&view, &hdr);
    }
    if (opts->show_dynamic) {
        print_dynamic(&view, &hdr);
    }
    if (opts->show_notes) {
        print_notes(&view, &hdr);
    }
    if (opts->show_version_info) {
        print_version_info(&view, &hdr);
    }
    if (opts->show_histogram) {
        print_hash_histogram(&view, &hdr);
    }
    if (opts->show_groups) {
        print_section_groups(&view, &hdr);
    }
    if (opts->show_unwind) {
        print_arm_unwind(&view, &hdr);
    }
    if (opts->show_arch_specific) {
        print_arm_attributes(&view, &hdr);
    }
    if (opts->hex_dump_target != NULL) {
        do_hex_dump(&view, &hdr, opts->hex_dump_target);
    }
    if (opts->str_dump_target != NULL) {
        do_string_dump(&view, &hdr, opts->str_dump_target);
    }
    if (opts->show_debug_dump) {
        print_debug_dump(&view, &hdr, opts->debug_dump_kind ? opts->debug_dump_kind : "all");
    }
    if (opts->show_core || hdr.type == ET_CORE) {
        print_core_info(&view, &hdr);
    }

out:
    free(buf);
    if (fp != NULL) {
        fclose(fp);
    }
    elf_close(obj);
    return rc;
}

int main(int argc, char **argv) {
    readelf_opts_t opts;
    int parse_rc;
    int rc = 0;
    int i;
    int input_count;

    if (argv != NULL && argv[0] != NULL && argv[0][0] != '\0') {
        g_progname = argv[0];
    }

    parse_rc = parse_options(argc, argv, &opts);
    if (parse_rc > 0) {
        return 0;
    }
    if (parse_rc < 0) {
        return 1;
    }

    input_count = argc - optind;
    if (input_count <= 0) {
        usage(stderr);
        return 1;
    }

    for (i = optind; i < argc; ++i) {
        if (process_file(argv[i], &opts, input_count > 1) != 0) {
            rc = 1;
        }
    }

    return rc;
}
