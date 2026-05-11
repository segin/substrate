/*
 * ldd — list dynamic dependencies of an ELF binary.
 *
 * Two paths:
 *
 *  1. NATIVE dynamic binaries (PT_INTERP=/sbin/ld.so).  Re-exec
 *     the target with LD_TRACE_LOADED_OBJECTS=1 set; /sbin/ld.so
 *     honours that and prints the loaded-object scope with REAL
 *     addresses (where each library actually mapped) instead of a
 *     placeholder.  FreeBSD-style — hand the work to the runtime
 *     linker rather than re-implementing it.
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
 * presence of PT_DYNAMIC + (for the trace path) PT_INTERP of
 * /sbin/ld.so.
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

#define SUBSTRATE_LDSO_INTERP "/sbin/ld.so"

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

#define EI_CLASS    4
#define EI_OSABI    7
#define ELFCLASS32  1

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

static void
build_search_paths(unsigned char abi, const char *interp,
                   const char **out, int out_max)
{
    int n = 0;
    int is_netbsd  = (abi == ELFOSABI_NETBSD)  ||
                     (interp && strstr(interp, "ld.elf_so"));
    int is_freebsd = (abi == ELFOSABI_FREEBSD) ||
                     (interp && strstr(interp, "ld-elf.so"));
    int is_linux   = (abi == ELFOSABI_LINUX)   ||
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
 * the target.  /sbin/ld.so loads everything as usual, sees the
 * env var, prints the loaded-object list with real addresses, then
 * exits 0 before handing control to the program.
 *
 * Returns 0 on a successful trace (whether the binary's exit code
 * was 0 or not — `ldd` reports deps regardless), and -1 if exec
 * itself failed (caller can then fall back to ELF parsing).
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
    size_t       len;
    void        *blob;
    Elf32_Ehdr  *eh;
    Elf32_Phdr  *ph;
    int          i, j;
    int          have_dyn = 0;
    int          have_interp = 0;
    const char  *interp = NULL;
    Elf32_Dyn   *dyn = NULL;
    size_t       dyn_count = 0;
    const char  *strtab = NULL;
    size_t       strsz = 0;
    const char  *search[8] = {NULL};
    unsigned char abi;
    int          missing = 0;

    blob = slurp(path, &len);
    if (blob == NULL) {
        fprintf(stderr, "ldd: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (len < sizeof(Elf32_Ehdr)) {
        fprintf(stderr, "ldd: %s: too small for ELF\n", path);
        free(blob);
        return -1;
    }
    eh = (Elf32_Ehdr *)blob;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        fprintf(stderr, "ldd: %s: not an ELF file\n", path);
        free(blob);
        return -1;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "ldd: %s: not ELFCLASS32 (this ldd is i386-only)\n",
                path);
        free(blob);
        return -1;
    }
    abi = eh->e_ident[EI_OSABI];

    if (eh->e_phoff + (uint32_t)eh->e_phnum * eh->e_phentsize > len) {
        fprintf(stderr, "ldd: %s: program headers out of bounds\n", path);
        free(blob);
        return -1;
    }
    ph = (Elf32_Phdr *)((char *)blob + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_INTERP) {
            if (ph[i].p_offset + ph[i].p_filesz <= len) {
                interp = (const char *)blob + ph[i].p_offset;
                have_interp = 1;
            }
        } else if (ph[i].p_type == PT_DYNAMIC) {
            if (ph[i].p_offset + ph[i].p_filesz <= len) {
                dyn = (Elf32_Dyn *)((char *)blob + ph[i].p_offset);
                dyn_count = ph[i].p_filesz / sizeof(Elf32_Dyn);
                have_dyn = 1;
            }
        }
    }

    if (!have_dyn) {
        printf("\tnot a dynamic executable [%s, e_type=%u]\n",
               osabi_name(abi), eh->e_type);
        free(blob);
        return 0;
    }

    {
        uint32_t strtab_va = 0;
        for (i = 0; i < (int)dyn_count; i++) {
            if (dyn[i].d_tag == DT_NULL) break;
            if (dyn[i].d_tag == DT_STRTAB) strtab_va = dyn[i].d_val;
            else if (dyn[i].d_tag == DT_STRSZ) strsz = dyn[i].d_val;
        }
        if (strtab_va == 0) {
            fprintf(stderr, "ldd: %s: no DT_STRTAB\n", path);
            free(blob);
            return -1;
        }
        for (i = 0; i < eh->e_phnum; i++) {
            if (ph[i].p_type != PT_LOAD) continue;
            if (strtab_va >= ph[i].p_vaddr &&
                strtab_va <  ph[i].p_vaddr + ph[i].p_filesz) {
                uint32_t off = ph[i].p_offset +
                               (strtab_va - ph[i].p_vaddr);
                if (off + strsz <= len) {
                    strtab = (const char *)blob + off;
                }
                break;
            }
        }
        if (strtab == NULL) {
            fprintf(stderr, "ldd: %s: DT_STRTAB unmappable\n", path);
            free(blob);
            return -1;
        }
    }

    /*
     * Native dynamic binary?  Hand off to ld.so via the trace
     * env var so we get real addresses.  Free the slurped buffer
     * first since execv will replace the address space anyway —
     * but the child fork'd from us inherits our memory, and we
     * don't want a 100 KB buffer per fork.
     */
    if (have_interp && strcmp(interp, SUBSTRATE_LDSO_INTERP) == 0) {
        free(blob);
        if (trace_via_ldso(path) == 0) {
            return 0;
        }
        /* trace failed (target probably exec-broken) — fall through
         * to ELF-parse path.  Re-slurp. */
        blob = slurp(path, &len);
        if (blob == NULL) {
            return -1;
        }
        eh = (Elf32_Ehdr *)blob;
        ph = (Elf32_Phdr *)((char *)blob + eh->e_phoff);
        /* dyn / strtab / strsz / interp pointers were into the old
         * blob — re-derive them.  Easier to re-do the whole walk. */
        have_interp = 0; have_dyn = 0; interp = NULL; dyn = NULL;
        strtab = NULL; strsz = 0; dyn_count = 0;
        for (i = 0; i < eh->e_phnum; i++) {
            if (ph[i].p_type == PT_INTERP) {
                interp = (const char *)blob + ph[i].p_offset;
                have_interp = 1;
            } else if (ph[i].p_type == PT_DYNAMIC) {
                dyn = (Elf32_Dyn *)((char *)blob + ph[i].p_offset);
                dyn_count = ph[i].p_filesz / sizeof(Elf32_Dyn);
                have_dyn = 1;
            }
        }
        if (have_dyn) {
            uint32_t strtab_va = 0;
            for (i = 0; i < (int)dyn_count; i++) {
                if (dyn[i].d_tag == DT_NULL) break;
                if (dyn[i].d_tag == DT_STRTAB) strtab_va = dyn[i].d_val;
                else if (dyn[i].d_tag == DT_STRSZ) strsz = dyn[i].d_val;
            }
            for (i = 0; i < eh->e_phnum; i++) {
                if (ph[i].p_type != PT_LOAD) continue;
                if (strtab_va >= ph[i].p_vaddr &&
                    strtab_va <  ph[i].p_vaddr + ph[i].p_filesz) {
                    strtab = (const char *)blob + ph[i].p_offset +
                             (strtab_va - ph[i].p_vaddr);
                    break;
                }
            }
        }
    }

    if (have_interp) {
        printf("\t[interpreter: %s, OSABI=%s]\n", interp, osabi_name(abi));
    } else {
        printf("\t[OSABI=%s]\n", osabi_name(abi));
    }

    build_search_paths(abi, interp,
                       search, (int)(sizeof(search)/sizeof(search[0])));

    for (i = 0; i < (int)dyn_count; i++) {
        const char *name;
        char        resolved[256];
        int         found = 0;
        if (dyn[i].d_tag == DT_NULL) break;
        if (dyn[i].d_tag != DT_NEEDED) continue;
        if (dyn[i].d_val >= strsz) continue;
        name = strtab + dyn[i].d_val;
        for (j = 0; search[j] != NULL; j++) {
            if (file_exists(search[j], name, resolved, sizeof(resolved))) {
                printf("\t%s => %s (0x?\?\?\?\?\?\?\?)\n", name, resolved);
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
