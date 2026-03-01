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
