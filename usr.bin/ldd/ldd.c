/*
 * ldd — list dynamic dependencies of an ELF binary.
 *
 * Two paths:
 *
 *  1. NATIVE dynamic binaries (PT_INTERP=/sbin/ld.so for a 32-bit
 *     file, /sbin/ld64.so for a 64-bit one).  Re-exec the target with
 *     LD_TRACE_LOADED_OBJECTS=1 set; the runtime linker honours that
 *     and prints the loaded-object scope with REAL addresses (where
 *     each library actually mapped) instead of a placeholder.
 *     FreeBSD-style — hand the work to the runtime linker rather than
 *     re-implementing it.
 *
 *  2. STATIC binaries and FOREIGN (NetBSD/FreeBSD/Linux) binaries.
 *     We can't trust a foreign runtime linker to honour our env
 *     var, and a static binary runs no linker at all.  For these,
 *     fall back to parsing the ELF directly: walk Phdrs for
 *     PT_DYNAMIC, dynamic for DT_NEEDED, resolve names against a
 *     perso-aware search-path list.  Addresses are reported as
 *     "0x????????" placeholders since we don't actually load.
 *
 * Path selection happens after an ELF header peek: ELFOSABI byte +
 * presence of PT_DYNAMIC + (for the trace path) the PT_INTERP of the
 * file's class.
 *
 * Both ELF classes are read by the same code: elf_scan() copies what
 * ldd needs out of the ELF32 or ELF64 headers into one class-neutral
 * record, so this program handles 64-bit files whichever way it was
 * built itself.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SUBSTRATE_LDSO_INTERP   "/sbin/ld.so"
#define SUBSTRATE_LDSO64_INTERP "/sbin/ld64.so"

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
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

typedef struct {
    int32_t  d_tag;
    uint32_t d_val;
} Elf32_Dyn;

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    int64_t  d_tag;
    uint64_t d_val;
} Elf64_Dyn;

#define EI_CLASS    4
#define EI_OSABI    7
#define ELFCLASS32  1
#define ELFCLASS64  2

#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3

#define DT_NULL     0
#define DT_NEEDED   1
#define DT_STRTAB   5
#define DT_STRSZ    10

#define ELFOSABI_NONE       0
#define ELFOSABI_NETBSD     2
#define ELFOSABI_LINUX      3
#define ELFOSABI_FREEBSD    9

/* A program header, in either class's terms. */
struct seg {
    uint32_t type;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t filesz;
};

/* What ldd needs to know about a file, whichever its class. */
struct elf_info {
    int            is64;
    unsigned char  abi;
    unsigned       e_type;
    const char    *interp;      /* PT_INTERP string, or NULL */
    int            have_dyn;
    const void    *dyn;         /* PT_DYNAMIC contents */
    size_t         dyn_count;
    const char    *strtab;      /* DT_STRTAB contents */
    uint64_t       strsz;
};

static const char *
osabi_name(unsigned char abi)
{
    switch (abi) {
        case ELFOSABI_NONE:    return "SysV/native";
        case ELFOSABI_NETBSD:  return "NetBSD";
        case ELFOSABI_LINUX:   return "Linux";
        case ELFOSABI_FREEBSD: return "FreeBSD";
        default:               return "unknown";
    }
}

static void *
slurp(const char *path, size_t *out_len)
{
    int         fd;
    struct stat st;
    void       *buf;
    ssize_t     got;
    size_t      have = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return NULL;
    }
    buf = malloc((size_t)st.st_size);
    if (buf == NULL) {
        close(fd);
        return NULL;
    }
    while (have < (size_t)st.st_size) {
        got = read(fd, (char *)buf + have, (size_t)st.st_size - have);
        if (got <= 0) {
            free(buf);
            close(fd);
            return NULL;
        }
        have += (size_t)got;
    }
    close(fd);
    *out_len = have;
    return buf;
}

/* True iff [off, off + size) lies inside a file of `len` bytes. */
static int
in_file(uint64_t off, uint64_t size, size_t len)
{
    return off <= len && size <= len - off;
}

/* Program header `i` of the file, read at its class's layout. */
static void
get_seg(const void *blob, const struct elf_info *ei, uint64_t phoff,
        unsigned phentsize, unsigned i, struct seg *out)
{
    const char *p = (const char *)blob + phoff + (uint64_t)i * phentsize;
    if (ei->is64) {
        Elf64_Phdr ph;
        memcpy(&ph, p, sizeof(ph));
        out->type = ph.p_type;
        out->offset = ph.p_offset;
        out->vaddr = ph.p_vaddr;
        out->filesz = ph.p_filesz;
    } else {
        Elf32_Phdr ph;
        memcpy(&ph, p, sizeof(ph));
        out->type = ph.p_type;
        out->offset = ph.p_offset;
        out->vaddr = ph.p_vaddr;
        out->filesz = ph.p_filesz;
    }
}

/* Entry `i` of the dynamic section. */
static void
get_dyn(const struct elf_info *ei, size_t i, int64_t *tag, uint64_t *val)
{
    if (ei->is64) {
        Elf64_Dyn d;
        memcpy(&d, (const char *)ei->dyn + i * sizeof(d), sizeof(d));
        *tag = d.d_tag;
        *val = d.d_val;
    } else {
        Elf32_Dyn d;
        memcpy(&d, (const char *)ei->dyn + i * sizeof(d), sizeof(d));
        *tag = d.d_tag;
        *val = d.d_val;
    }
}

/*
 * Read the headers of the `len`-byte file image `blob` into *ei.  The
 * pointers stored there point into blob.  Returns 0, or -1 after a
 * diagnostic naming `path`.  A file with no PT_DYNAMIC is not an error:
 * have_dyn is left 0 and the string table is not looked for.
 */
static int
elf_scan(const char *path, const void *blob, size_t len, struct elf_info *ei)
{
    const unsigned char *ident = (const unsigned char *)blob;
    uint64_t    phoff;
    unsigned    phnum, phentsize, minent;
    unsigned    i;
    uint64_t    strtab_va = 0;
    int         have_strtab = 0;
    size_t      n;

    memset(ei, 0, sizeof(*ei));
    if (len < sizeof(Elf32_Ehdr)) {
        fprintf(stderr, "ldd: %s: too small for ELF\n", path);
        return -1;
    }
    if (ident[0] != 0x7f || ident[1] != 'E' ||
        ident[2] != 'L'  || ident[3] != 'F') {
        fprintf(stderr, "ldd: %s: not an ELF file\n", path);
        return -1;
    }
    ei->abi = ident[EI_OSABI];
    if (ident[EI_CLASS] == ELFCLASS64) {
        Elf64_Ehdr eh;
        if (len < sizeof(eh)) {
            fprintf(stderr, "ldd: %s: too small for ELF\n", path);
            return -1;
        }
        memcpy(&eh, blob, sizeof(eh));
        ei->is64 = 1;
        ei->e_type = eh.e_type;
        phoff = eh.e_phoff;
        phnum = eh.e_phnum;
        phentsize = eh.e_phentsize;
        minent = sizeof(Elf64_Phdr);
    } else if (ident[EI_CLASS] == ELFCLASS32) {
        Elf32_Ehdr eh;
        memcpy(&eh, blob, sizeof(eh));
        ei->e_type = eh.e_type;
        phoff = eh.e_phoff;
        phnum = eh.e_phnum;
        phentsize = eh.e_phentsize;
        minent = sizeof(Elf32_Phdr);
    } else {
        fprintf(stderr, "ldd: %s: unknown ELF class %u\n", path,
                ident[EI_CLASS]);
        return -1;
    }

    if ((phnum != 0 && phentsize < minent) ||
        !in_file(phoff, (uint64_t)phnum * phentsize, len)) {
        fprintf(stderr, "ldd: %s: program headers out of bounds\n", path);
        return -1;
    }
    for (i = 0; i < phnum; i++) {
        struct seg s;
        get_seg(blob, ei, phoff, phentsize, i, &s);
        if (!in_file(s.offset, s.filesz, len)) {
            continue;
        }
        if (s.type == PT_INTERP) {
            /* Only a terminated string can be compared and printed. */
            if (s.filesz > 0 &&
                ((const char *)blob)[s.offset + s.filesz - 1] == '\0') {
                ei->interp = (const char *)blob + s.offset;
            }
        } else if (s.type == PT_DYNAMIC) {
            ei->dyn = (const char *)blob + s.offset;
            ei->dyn_count = (size_t)(s.filesz /
                (ei->is64 ? sizeof(Elf64_Dyn) : sizeof(Elf32_Dyn)));
            ei->have_dyn = 1;
        }
    }
    if (!ei->have_dyn) {
        return 0;
    }

    for (n = 0; n < ei->dyn_count; n++) {
        int64_t  tag;
        uint64_t val;
        get_dyn(ei, n, &tag, &val);
        if (tag == DT_NULL) break;
        if (tag == DT_STRTAB) { strtab_va = val; have_strtab = 1; }
        else if (tag == DT_STRSZ) ei->strsz = val;
    }
    if (!have_strtab || strtab_va == 0) {
        fprintf(stderr, "ldd: %s: no DT_STRTAB\n", path);
        return -1;
    }
    /* DT_STRTAB is an address; find the file bytes behind it. */
    for (i = 0; i < phnum; i++) {
        struct seg s;
        get_seg(blob, ei, phoff, phentsize, i, &s);
        if (s.type != PT_LOAD) continue;
        if (strtab_va >= s.vaddr && strtab_va - s.vaddr < s.filesz) {
            uint64_t off = s.offset + (strtab_va - s.vaddr);
            if (in_file(off, ei->strsz, len)) {
                ei->strtab = (const char *)blob + off;
            }
            break;
        }
    }
    if (ei->strtab == NULL) {
        fprintf(stderr, "ldd: %s: DT_STRTAB unmappable\n", path);
        return -1;
    }
    return 0;
}

static void
build_search_paths(const struct elf_info *ei, const char **out, int out_max)
{
    int n = 0;
    const char *interp = ei->interp;
    int is_netbsd  = (ei->abi == ELFOSABI_NETBSD)  ||
                     (interp && strstr(interp, "ld.elf_so"));
    int is_freebsd = (ei->abi == ELFOSABI_FREEBSD) ||
                     (interp && strstr(interp, "ld-elf.so"));
    int is_linux   = (ei->abi == ELFOSABI_LINUX)   ||
                     (interp && strstr(interp, "ld-linux"));

    if (is_netbsd) {
        if (n < out_max) out[n++] = "/perso/netbsd/lib";
        if (n < out_max) out[n++] = "/perso/netbsd/usr/lib";
        if (n < out_max) out[n++] = "/perso/netbsd/usr/pkg/lib";
    } else if (is_freebsd) {
        if (n < out_max) out[n++] = "/perso/freebsd/lib";
        if (n < out_max) out[n++] = "/perso/freebsd/usr/lib";
    } else if (is_linux) {
        if (n < out_max) out[n++] = "/perso/linux/lib";
        if (n < out_max) out[n++] = "/perso/linux/usr/lib";
    } else if (ei->is64) {
        /* The built-in directories of /sbin/ld64.so. */
        if (n < out_max) out[n++] = "/lib64";
        if (n < out_max) out[n++] = "/usr/lib64";
        if (n < out_max) out[n++] = "/usr/local/lib64";
    } else {
        if (n < out_max) out[n++] = "/lib";
        if (n < out_max) out[n++] = "/usr/lib";
        if (n < out_max) out[n++] = "/usr/local/lib";
    }
    if (n < out_max) out[n] = NULL;
}

static int
file_exists(const char *dir, const char *name, char *out, size_t out_max)
{
    struct stat st;
    int         n = snprintf(out, out_max, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= out_max) {
        return 0;
    }
    return stat(out, &st) == 0 && S_ISREG(st.st_mode);
}

/*
 * Native trace path: fork, set LD_TRACE_LOADED_OBJECTS=1, execv
 * the target.  The runtime linker loads everything as usual, sees the
 * env var, prints the loaded-object list with real addresses, then
 * exits 0 before handing control to the program.
 *
 * Returns 0 on a successful trace (whether the binary's exit code
 * was 0 or not — `ldd` reports deps regardless), and -1 if exec
 * itself failed (caller can then fall back to ELF parsing).  A 64-bit
 * program on the 32-bit kernel fails that way.
 */
static int
trace_via_ldso(const char *path)
{
    pid_t pid = fork();
    int   status = 0;
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        setenv("LD_TRACE_LOADED_OBJECTS", "1", 1);
        {
            char *const argv[] = { (char *)path, NULL };
            execv(path, argv);
        }
        /* exec failed — signal via non-zero exit so the parent's
         * waitpid sees it.  127 by sh convention. */
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        return -1;  /* exec failed in the child */
    }
    return 0;
}

static int
do_ldd(const char *path)
{
    size_t          len;
    void           *blob;
    struct elf_info ei;
    const char     *search[8] = {NULL};
    const char     *unknown_addr;
    struct stat     pst;
    int             setid;
    int             missing = 0;
    size_t          i;
    int             j;

    blob = slurp(path, &len);
    if (blob == NULL) {
        fprintf(stderr, "ldd: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (elf_scan(path, blob, len, &ei) != 0) {
        free(blob);
        return -1;
    }

    if (!ei.have_dyn) {
        printf("\tnot a dynamic executable [%s, e_type=%u]\n",
               osabi_name(ei.abi), ei.e_type);
        free(blob);
        return 0;
    }

    /*
     * Native dynamic binary?  Hand off to the runtime linker via the
     * trace env var so we get real addresses.
     *
     * Never for a setuid or setgid file, though: the linker ignores
     * LD_TRACE_LOADED_OBJECTS in a program it runs with privileges, so
     * the program would simply run, with those privileges, for whoever
     * ran ldd.  Its headers are read instead, below.
     */
    setid = stat(path, &pst) != 0 || (pst.st_mode & (S_ISUID | S_ISGID));
    if (ei.interp != NULL && !setid &&
        strcmp(ei.interp, ei.is64 ? SUBSTRATE_LDSO64_INTERP
                                  : SUBSTRATE_LDSO_INTERP) == 0) {
        if (trace_via_ldso(path) == 0) {
            free(blob);
            return 0;
        }
        /* trace failed (target probably exec-broken) — fall through
         * to the ELF-parse path. */
    }

    if (ei.interp != NULL) {
        printf("\t[interpreter: %s, OSABI=%s]\n", ei.interp,
               osabi_name(ei.abi));
    } else {
        printf("\t[OSABI=%s]\n", osabi_name(ei.abi));
    }

    build_search_paths(&ei, search, (int)(sizeof(search)/sizeof(search[0])));
    unknown_addr = ei.is64 ? "0x????????????????" : "0x????????";

    for (i = 0; i < ei.dyn_count; i++) {
        const char *name;
        char        resolved[256];
        int         found = 0;
        int64_t     tag;
        uint64_t    val;

        get_dyn(&ei, i, &tag, &val);
        if (tag == DT_NULL) break;
        if (tag != DT_NEEDED) continue;
        if (val >= ei.strsz) continue;
        /* The name must end inside the string table. */
        if (memchr(ei.strtab + val, '\0', (size_t)(ei.strsz - val)) == NULL)
            continue;
        name = ei.strtab + val;
        for (j = 0; search[j] != NULL; j++) {
            if (file_exists(search[j], name, resolved, sizeof(resolved))) {
                printf("\t%s => %s (%s)\n", name, resolved, unknown_addr);
                found = 1;
                break;
            }
        }
        if (!found) {
            printf("\t%s => not found\n", name);
            missing = 1;
        }
    }

    free(blob);
    return missing ? 1 : 0;
}

static void
usage(const char *p)
{
    fprintf(stderr, "usage: %s [--help] FILE [FILE ...]\n", p);
}

int
main(int argc, char **argv)
{
    int fails = 0;
    int i;
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }
    for (i = 1; i < argc; i++) {
        if (argc > 2) {
            printf("%s:\n", argv[i]);
        }
        if (do_ldd(argv[i]) < 0) {
            fails++;
        }
    }
    return fails ? 1 : 0;
}
