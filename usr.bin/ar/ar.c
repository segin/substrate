/*
 * Substrate ar(1) - Archive utility
 *
 * Implements creation, modification, and extraction of archives.
 * Supports BSD/SVR4 format, extended filenames, and symbol tables.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <elf.h>

#include "ar.h"

#ifndef ELFMAG
#define ELFMAG      "\177ELF"
#endif
#ifndef SELFMAG
#define SELFMAG     4
#endif

#ifndef STB_GLOBAL
#define STB_GLOBAL  1
#endif
#ifndef STB_WEAK
#define STB_WEAK    2
#endif
#ifndef SHN_UNDEF
#define SHN_UNDEF   0
#endif

#ifndef _ELF_H_SHDR
#define _ELF_H_SHDR
/* Section header might be missing from minimal elf.h */
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;
#endif

/* Mode flags */
#define AR_APPEND       0x0001
#define AR_CREATE       0x0002
#define AR_DELETE       0x0004
#define AR_EXTRACT      0x0008
#define AR_LIST         0x0010
#define AR_MOVE         0x0020
#define AR_PRINT        0x0040
#define AR_REPLACE      0x0080
#define AR_TABLE        0x0100

/* Modifiers */
#define MOD_VERBOSE     0x0001
#define MOD_UPDATE      0x0002
#define MOD_COUNT       0x0008

static char *archive_name;
static int operation = 0;
static int modifiers = 0;
static const char *progname;

/* Structure to hold in-memory archive member */
struct ar_memb {
    struct ar_hdr hdr;
    char *name;
    void *data;
    size_t size;
    struct ar_memb *next;
    bool dirty;
    bool deleted;
};

static struct ar_memb *head = NULL;

/* Symbol table entry */
struct sym_entry {
    char *name;
    uint32_t offset; /* File offset of member header */
    struct ar_memb *member; /* Pointer to member */
    struct sym_entry *next;
};

static void usage(void);
static void warn(const char *fmt, ...);
static void warnx(const char *fmt, ...);
static void err(int eval, const char *fmt, ...);
static void errx(int eval, const char *fmt, ...);
static const char *get_basename(const char *path);

static void read_archive(const char *path);
static void write_archive(const char *path);
static void append_files(char **files, int count);
static void extract_members(char **members, int count);
static void list_members(char **members, int count);
static void delete_members(char **members, int count);
static void move_members(char **members, int count);
static void print_members(char **members, int count);
static void ranlib(void);

int main(int argc, char *argv[])
{
    progname = get_basename(argv[0]);

    if (strstr(progname, "ranlib")) {
        if (argc < 2) usage();
        operation = AR_TABLE;
        archive_name = argv[1];
        if (access(archive_name, F_OK) != 0) err(1, "%s", archive_name);
        read_archive(archive_name);
        ranlib();
        write_archive(archive_name);
        return 0;
    }

    if (argc < 2) usage();

    int argi = 1;
    char *key = argv[argi];
    if (key[0] == '-') key++;

    while (*key) {
        switch (*key) {
        case 'r': operation |= AR_REPLACE; break;
        case 'c': modifiers |= AR_CREATE; break;
        case 't': operation |= AR_LIST; break;
        case 'x': operation |= AR_EXTRACT; break;
        case 'd': operation |= AR_DELETE; break;
        case 'q': operation |= AR_APPEND; break;
        case 'm': operation |= AR_MOVE; break;
        case 'p': operation |= AR_PRINT; break;
        case 's': modifiers |= AR_TABLE; break;
        case 'v': modifiers |= MOD_VERBOSE; break;
        case 'u': modifiers |= MOD_UPDATE; break;
        default:
            warnx("illegal option -- %c", *key);
            usage();
        }
        key++;
    }

    argi++;
    if (argi >= argc && (operation & (AR_DELETE|AR_EXTRACT|AR_LIST)) == 0 && !(operation == 0 && (modifiers & AR_TABLE))) {
        /* Need archive name at least */
        if (argi == argc) usage();
    }

    if (argi < argc) {
        archive_name = argv[argi++];
    } else {
        usage();
    }

    char **files = &argv[argi];
    int file_count = argc - argi;

    /* Normalize operations */
    if (operation == 0 && (modifiers & AR_TABLE)) operation = AR_TABLE;

    int op_count = 0;
    if (operation & AR_REPLACE) op_count++;
    if (operation & AR_LIST) op_count++;
    if (operation & AR_EXTRACT) op_count++;
    if (operation & AR_DELETE) op_count++;
    if (operation & AR_APPEND) op_count++;
    if (operation & AR_MOVE) op_count++;
    if (operation & AR_PRINT) op_count++;
    if (operation & AR_TABLE && op_count == 0) op_count++; /* ranlib only */

    if (op_count > 1) errx(1, "only one operation can be specified");
    if (op_count == 0) usage();

    if (operation != AR_APPEND && operation != AR_REPLACE && operation != AR_CREATE) {
        read_archive(archive_name);
    } else {
        if (access(archive_name, F_OK) == 0) {
            read_archive(archive_name);
        } else {
            if (!(operation & AR_REPLACE) && !(operation & AR_APPEND)) err(1, "%s", archive_name);
            if (!(modifiers & AR_CREATE)) warnx("creating %s", archive_name);
        }
    }

    switch (operation) {
    case AR_REPLACE:
    case AR_APPEND:
        append_files(files, file_count);
        if (modifiers & AR_TABLE) ranlib();
        write_archive(archive_name);
        break;
    case AR_DELETE:
        delete_members(files, file_count);
        if (modifiers & AR_TABLE) ranlib();
        write_archive(archive_name);
        break;
    case AR_EXTRACT:
        extract_members(files, file_count);
        break;
    case AR_LIST:
        list_members(files, file_count);
        break;
    case AR_MOVE:
        move_members(files, file_count);
        if (modifiers & AR_TABLE) ranlib();
        write_archive(archive_name);
        break;
    case AR_PRINT:
        print_members(files, file_count);
        break;
    case AR_TABLE:
        ranlib();
        write_archive(archive_name);
        break;
    }

    return 0;
}

static void usage(void) {
    fprintf(stderr, "usage: ar [drqtpmx][lsvV] archive [member...]\n");
    exit(1);
}

static void warn(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s\n", strerror(errno));
}

static void warnx(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void err(int eval, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s\n", strerror(errno));
    exit(eval);
}

static void errx(int eval, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(eval);
}

static const char *get_basename(const char *path) {
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

static void read_archive(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) return;
        err(1, "%s", path);
    }

    char magic[8];
    if (read(fd, magic, 8) != 8 || memcmp(magic, ARMAG, 8) != 0) {
        errx(1, "%s: file format not recognized", path);
    }

    struct ar_hdr hdr;
    while (read(fd, &hdr, sizeof(hdr)) == sizeof(hdr)) {
        if (memcmp(hdr.ar_fmag, ARFMAG, 2) != 0) {
            warnx("malformed header in %s", path);
            break;
        }

        long size = atol(hdr.ar_size);

        struct ar_memb *m = malloc(sizeof(struct ar_memb));
        m->hdr = hdr;
        m->next = NULL;
        m->deleted = false;
        m->dirty = false;

        if (memcmp(hdr.ar_name, "#1/", 3) == 0) {
            int name_len = atoi(hdr.ar_name + 3);
            m->name = malloc(name_len + 1);
            read(fd, m->name, name_len);
            m->name[name_len] = 0;
            m->size = size - name_len;
            m->data = malloc(m->size);
            read(fd, m->data, m->size);
        } else {
            char tmp[17];
            memcpy(tmp, hdr.ar_name, 16);
            tmp[16] = 0;
            char *slash = strchr(tmp, '/');
            if (slash) *slash = 0;
            for (int i=15; i>=0; i--) {
                if (tmp[i] == ' ') tmp[i] = 0;
                else break;
            }
            m->name = strdup(tmp);
            m->size = size;
            m->data = malloc(size);
            read(fd, m->data, size);
        }

        if (size % 2 != 0) lseek(fd, 1, SEEK_CUR);

        if (head == NULL) head = m;
        else {
            struct ar_memb *cur = head;
            while (cur->next) cur = cur->next;
            cur->next = m;
        }
    }
    close(fd);
}

static void append_files(char **files, int count) {
    for (int i = 0; i < count; i++) {
        const char *fname = files[i];
        int fd = open(fname, O_RDONLY);
        if (fd < 0) {
            warn("%s", fname);
            continue;
        }

        struct stat st;
        fstat(fd, &st);

        void *data = malloc(st.st_size);
        read(fd, data, st.st_size);
        close(fd);

        const char *base = get_basename(fname);
        struct ar_memb *m = NULL;
        struct ar_memb *cur = head;
        while (cur) {
            if (strcmp(cur->name, base) == 0) {
                m = cur;
                break;
            }
            cur = cur->next;
        }

        if (operation == AR_REPLACE && m) {
            if (modifiers & MOD_UPDATE) {
                long old_time = atol(m->hdr.ar_date);
                if (st.st_mtime <= old_time) {
                    free(data);
                    continue;
                }
            }
            if (modifiers & MOD_VERBOSE) printf("r - %s\n", base);
            free(m->data);
            free(m->name);
        } else {
            if (modifiers & MOD_VERBOSE) printf("a - %s\n", base);
            m = malloc(sizeof(struct ar_memb));
            m->next = NULL;
            if (head == NULL) head = m;
            else {
                struct ar_memb *last = head;
                while (last->next) last = last->next;
                last->next = m;
            }
        }

        m->data = data;
        m->size = st.st_size;
        m->name = strdup(base);
        m->dirty = true;
        m->deleted = false;

        memset(&m->hdr, ' ', sizeof(struct ar_hdr));
        char buf[32];
        snprintf(buf, sizeof(buf), "%-12ld", (long)st.st_mtime);
        memcpy(m->hdr.ar_date, buf, 12);
        snprintf(buf, sizeof(buf), "%-6d", (int)st.st_uid);
        memcpy(m->hdr.ar_uid, buf, 6);
        snprintf(buf, sizeof(buf), "%-6d", (int)st.st_gid);
        memcpy(m->hdr.ar_gid, buf, 6);
        snprintf(buf, sizeof(buf), "%-8o", (int)st.st_mode);
        memcpy(m->hdr.ar_mode, buf, 8);
        snprintf(buf, sizeof(buf), "%-10ld", (long)st.st_size);
        memcpy(m->hdr.ar_size, buf, 10);
        memcpy(m->hdr.ar_fmag, ARFMAG, 2);
    }
}

static void delete_members(char **members, int count) {
    if (count == 0) return;
    for (int i = 0; i < count; i++) {
        struct ar_memb *cur = head;
        bool found = false;
        while (cur) {
            if (strcmp(cur->name, members[i]) == 0) {
                if (modifiers & MOD_VERBOSE) printf("d - %s\n", cur->name);
                cur->deleted = true;
                found = true;
            }
            cur = cur->next;
        }
        if (!found) warnx("%s: not found in archive", members[i]);
    }
}

static void move_members(char **members, int count) {
    if (count == 0) return;
    for (int i = 0; i < count; i++) {
        struct ar_memb *cur = head;
        struct ar_memb *prev = NULL;
        struct ar_memb *target = NULL;

        while (cur) {
            if (strcmp(cur->name, members[i]) == 0 && !cur->deleted) {
                target = cur;
                if (prev) prev->next = cur->next;
                else head = cur->next;
                break;
            }
            prev = cur;
            cur = cur->next;
        }

        if (target) {
            if (modifiers & MOD_VERBOSE) printf("m - %s\n", target->name);
            target->next = NULL;
            if (head == NULL) head = target;
            else {
                struct ar_memb *last = head;
                while (last->next) last = last->next;
                last->next = target;
            }
        } else {
            warnx("%s: not found", members[i]);
        }
    }
}

static void print_members(char **members, int count) {
    struct ar_memb *cur = head;
    while (cur) {
        if (cur->deleted) { cur = cur->next; continue; }
        if (strcmp(cur->name, RANLIBMAG) == 0) { cur = cur->next; continue; }

        bool match = (count == 0);
        for (int i = 0; i < count; i++) if (strcmp(cur->name, members[i]) == 0) match = true;

        if (match) {
            if (modifiers & MOD_VERBOSE) printf("p - %s\n", cur->name);
            fwrite(cur->data, cur->size, 1, stdout);
        }
        cur = cur->next;
    }
}

static void extract_members(char **members, int count) {
    struct ar_memb *cur = head;
    while (cur) {
        if (cur->deleted) { cur = cur->next; continue; }
        bool match = (count == 0);
        for (int i = 0; i < count; i++) {
            if (strcmp(cur->name, members[i]) == 0) {
                match = true;
                break;
            }
        }
        if (match) {
            const char *base = get_basename(cur->name);
            if (modifiers & MOD_VERBOSE) printf("x - %s\n", base);
            int fd = open(base, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) warn("%s", base);
            else {
                write(fd, cur->data, cur->size);
                close(fd);
            }
        }
        cur = cur->next;
    }
}

static void list_members(char **members, int count) {
    struct ar_memb *cur = head;
    while (cur) {
        if (cur->deleted) { cur = cur->next; continue; }
        if (strcmp(cur->name, RANLIBMAG) == 0 || strcmp(cur->name, RANLIBSORT) == 0 ||
            strcmp(cur->name, "/") == 0 || strcmp(cur->name, "//") == 0) {
            cur = cur->next; continue;
        }
        bool match = (count == 0);
        for (int i = 0; i < count; i++) {
            if (strcmp(cur->name, members[i]) == 0) {
                match = true;
                break;
            }
        }
        if (match) {
            if (modifiers & MOD_VERBOSE) {
                int m = strtol(cur->hdr.ar_mode, NULL, 8);
                printf("%c%c%c%c%c%c%c%c%c %ld/%ld %6ld %s %s\n",
                    (m & S_IRUSR) ? 'r' : '-', (m & S_IWUSR) ? 'w' : '-', (m & S_IXUSR) ? 'x' : '-',
                    (m & S_IRGRP) ? 'r' : '-', (m & S_IWGRP) ? 'w' : '-', (m & S_IXGRP) ? 'x' : '-',
                    (m & S_IROTH) ? 'r' : '-', (m & S_IWOTH) ? 'w' : '-', (m & S_IXOTH) ? 'x' : '-',
                    strtol(cur->hdr.ar_uid, NULL, 10), strtol(cur->hdr.ar_gid, NULL, 10),
                    (long)cur->size, "date", cur->name);
            } else {
                printf("%s\n", cur->name);
            }
        }
        cur = cur->next;
    }
}

static void get_elf_symbols(struct ar_memb *m, struct sym_entry **sym_head) {
    if (m->size < sizeof(Elf32_Ehdr)) return;
    Elf32_Ehdr *eh = (Elf32_Ehdr *)m->data;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) return;

    Elf32_Shdr *sh = (Elf32_Shdr *)((char *)m->data + eh->e_shoff);
    Elf32_Shdr *symtab = NULL;
    Elf32_Shdr *strtab = NULL;

    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type == SHT_SYMTAB) {
            symtab = &sh[i];
            if (sh[i].sh_link < eh->e_shnum) strtab = &sh[sh[i].sh_link];
            break;
        }
    }

    if (!symtab || !strtab) return;

    Elf32_Sym *syms = (Elf32_Sym *)((char *)m->data + symtab->sh_offset);
    char *strs = (char *)m->data + strtab->sh_offset;
    int num_syms = symtab->sh_size / sizeof(Elf32_Sym);

    for (int i = 0; i < num_syms; i++) {
        unsigned char bind = ELF32_ST_BIND(syms[i].st_info);
        if (bind == STB_GLOBAL || bind == STB_WEAK) {
            if (syms[i].st_shndx != SHN_UNDEF) {
                char *name = strs + syms[i].st_name;
                if (*name) {
                    struct sym_entry *s = malloc(sizeof(struct sym_entry));
                    s->name = strdup(name);
                    s->offset = 0;
                    s->member = m;
                    s->next = *sym_head;
                    *sym_head = s;
                }
            }
        }
    }
}

static int compare_syms(const void *a, const void *b) {
    struct sym_entry *const *sa = a;
    struct sym_entry *const *sb = b;
    return strcmp((*sa)->name, (*sb)->name);
}

static void ranlib(void) {
    struct ar_memb *cur = head;
    struct ar_memb *prev = NULL;
    while (cur) {
        if (strcmp(cur->name, RANLIBMAG) == 0 || strcmp(cur->name, RANLIBSORT) == 0 ||
            strcmp(cur->name, "/") == 0 || strcmp(cur->name, "//") == 0) {
            if (prev) prev->next = cur->next;
            else head = cur->next;
            cur = cur->next;
            continue;
        }
        prev = cur;
        cur = cur->next;
    }

    struct sym_entry *list = NULL;
    int count = 0;
    cur = head;
    while (cur) {
        if (!cur->deleted) get_elf_symbols(cur, &list);
        cur = cur->next;
    }

    if (list == NULL) return;

    struct sym_entry *s = list;
    while (s) { count++; s = s->next; }

    /* Convert to array for sorting */
    struct sym_entry **arr = malloc(count * sizeof(struct sym_entry*));
    s = list;
    for (int i = 0; i < count; i++) {
        arr[i] = s;
        s = s->next;
    }

    qsort(arr, count, sizeof(struct sym_entry*), compare_syms);

    int strsize = 0;
    for (int i = 0; i < count; i++) strsize += strlen(arr[i]->name) + 1;

    uint32_t array_size = count * sizeof(struct ranlib);
    size_t total_size = sizeof(uint32_t) + array_size + sizeof(uint32_t) + strsize;

    /* Create dummy data to reserve space */
    void *data = calloc(1, total_size);
    /* We'll fill offsets in write_archive */

    struct ar_memb *m = malloc(sizeof(struct ar_memb));
    m->name = strdup(RANLIBMAG);
    m->data = data;
    m->size = total_size;
    m->deleted = false;
    m->dirty = true;
    memset(&m->hdr, ' ', sizeof(struct ar_hdr));
    char buf[32];
    snprintf(buf, sizeof(buf), "%-12ld", (long)time(NULL));
    memcpy(m->hdr.ar_date, buf, 12);
    snprintf(buf, sizeof(buf), "%-6d", 0);
    memcpy(m->hdr.ar_uid, buf, 6);
    snprintf(buf, sizeof(buf), "%-6d", 0);
    memcpy(m->hdr.ar_gid, buf, 6);
    snprintf(buf, sizeof(buf), "%-8o", 0644);
    memcpy(m->hdr.ar_mode, buf, 8);
    snprintf(buf, sizeof(buf), "%-10ld", (long)total_size);
    memcpy(m->hdr.ar_size, buf, 10);
    memcpy(m->hdr.ar_fmag, ARFMAG, 2);
    snprintf(buf, sizeof(buf), "%-16s", RANLIBMAG);
    memcpy(m->hdr.ar_name, buf, 16);

    m->next = head;
    head = m;

    /* cleanup */
    /* We don't free sym entries because we need them?
       No, write_archive will regenerate them.
       Actually, write_archive needs sorting too!
       It's better to sort in write_archive.
       Here we just reserve the correct size.
       Is the size dependent on sorting? No.
       Is the order important for size? No.
    */

    free(arr);
    /* Free list */
    while (list) {
        struct sym_entry *n = list->next;
        free(list->name);
        free(list);
        list = n;
    }
}

static void write_archive(const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) err(1, "fopen %s", path);

    fprintf(fp, ARMAG);
    uint32_t offset = 8;

    struct ar_memb *symdef = NULL;
    if (head && strcmp(head->name, RANLIBMAG) == 0) symdef = head;

    struct ar_memb *cur = head;

    /* Pre-calc offsets for symbol table */
    if (symdef) {
        /* Need to rebuild sorted symbol table */
        struct sym_entry *list = NULL;
        int count = 0;

        /* Calculate initial offset past symdef */
        uint32_t current_off = offset + sizeof(struct ar_hdr) + symdef->size + (symdef->size % 2);

        struct ar_memb *m = symdef->next;
        while (m) {
            if (!m->deleted) {
                uint32_t m_off = current_off;

                struct sym_entry *s_head = NULL;
                get_elf_symbols(m, &s_head);
                /* Add to list with offset */
                while (s_head) {
                    struct sym_entry *next = s_head->next;
                    s_head->offset = m_off;
                    s_head->next = list;
                    list = s_head;
                    count++;
                    s_head = next;
                }

                int name_len = strlen(m->name);
                bool extended = (name_len > 15 || strchr(m->name, ' '));
                long m_size = m->size;
                if (extended) m_size += name_len;
                current_off += sizeof(struct ar_hdr) + m_size + (m_size % 2);
            }
            m = m->next;
        }

        /* Convert to array and sort */
        if (count > 0) {
            struct sym_entry **arr = malloc(count * sizeof(struct sym_entry*));
            struct sym_entry *s = list;
            for (int i = 0; i < count; i++) {
                arr[i] = s;
                s = s->next;
            }
            qsort(arr, count, sizeof(struct sym_entry*), compare_syms);

            /* Fill symdef data */
            char *p = (char *)symdef->data;
            int strsize = 0;
            for (int i = 0; i < count; i++) strsize += strlen(arr[i]->name) + 1;

            uint32_t array_size = count * sizeof(struct ranlib);
            *(uint32_t *)p = array_size; p += 4;
            struct ranlib *ra = (struct ranlib *)p;
            p += array_size;
            *(uint32_t *)p = strsize; p += 4;
            char *strp = p;

            int stroff = 0;
            for (int i = 0; i < count; i++) {
                ra[i].ran_un.ran_strx = stroff;
                ra[i].ran_off = arr[i]->offset;
                strcpy(strp + stroff, arr[i]->name);
                stroff += strlen(arr[i]->name) + 1;
            }
            free(arr);
        }
        /* Cleanup list */
        while (list) {
            struct sym_entry *n = list->next;
            free(list->name);
            free(list);
            list = n;
        }
    }

    cur = head;
    while (cur) {
        if (cur->deleted) { cur = cur->next; continue; }

        int name_len = strlen(cur->name);
        bool extended = (name_len > 15 || strchr(cur->name, ' '));

        if (extended) {
            long data_len = cur->size + name_len;
            char buf[32];
            snprintf(buf, sizeof(buf), "%-10ld", data_len);
            memcpy(cur->hdr.ar_size, buf, 10);

            char tmp[32];
            snprintf(tmp, sizeof(tmp), "#1/%-12d", name_len);
            memcpy(cur->hdr.ar_name, tmp, 16);
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%-16s", cur->name);
            memcpy(cur->hdr.ar_name, buf, 16);
        }

        fwrite(&cur->hdr, sizeof(struct ar_hdr), 1, fp);
        if (extended) fwrite(cur->name, name_len, 1, fp);
        fwrite(cur->data, cur->size, 1, fp);

        long total_data = cur->size + (extended ? name_len : 0);
        if (total_data % 2 != 0) fputc('\n', fp);

        cur = cur->next;
    }
    fclose(fp);
}
