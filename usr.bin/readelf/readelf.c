#include <elfobj.h>

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

typedef struct {
    int wide;
    int show_file_header;
    int show_program_headers;
    int show_section_headers;
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
            "  -e, --headers           Equivalent to -h -l -S\n"
            "  -a, --all               Display all core information\n"
            "  -W, --wide              Do not limit output width\n"
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
        {"headers", no_argument, NULL, 'e'},
        {"all", no_argument, NULL, 'a'},
        {"wide", no_argument, NULL, 'W'},
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
    while ((ch = getopt_long(argc, argv, "hlSeaW", longopts, NULL)) != -1) {
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
            case 'e':
                opts->show_file_header = 1;
                opts->show_program_headers = 1;
                opts->show_section_headers = 1;
                break;
            case 'a':
                opts->show_file_header = 1;
                opts->show_program_headers = 1;
                opts->show_section_headers = 1;
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

    if (!opts->show_file_header && !opts->show_program_headers && !opts->show_section_headers) {
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
