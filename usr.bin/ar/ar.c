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
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "elfobj.h"

#include "ar.h"

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
#define MOD_AFTER       0x0004
#define MOD_COUNT       0x0008
#define MOD_BEFORE      0x0010
#define MOD_PRESERVE    0x0020
#define MOD_LOCAL       0x0040
#define MOD_DETERMINISTIC 0x0080
#define MOD_NOSYMTAB    0x0400
#define MOD_THIN        0x0800
#define MOD_NOCLOBBER   0x1000

static char *archive_name;
static int operation = 0;
static int modifiers = 0;
static const char *progname;
static const char *position_member = NULL;
static bool no_same_owner = false;
static unsigned long instance_count = 1;
typedef enum {
    ARFMT_BSD = 0,
    ARFMT_GNU = 1
} ar_format_t;
static ar_format_t archive_format = ARFMT_GNU;
static bool format_forced = false;
static char *gnu_name_table = NULL;
static size_t gnu_name_table_size = 0;

/* Structure to hold in-memory archive member */
struct ar_memb {
    struct ar_hdr hdr;
    char *name;
    void *data;
    char *thin_path;
    size_t size;
    size_t gnu_name_off;
    struct ar_memb *next;
    bool dirty;
    bool deleted;
    bool thin_ref;
    bool gnu_name_ref;
};

static struct ar_memb *head = NULL;
static bool archive_is_thin = false;

/* Symbol table entry */
struct sym_entry {
    char *name;
    uint64_t offset; /* File offset of member header */
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
static void drop_symbol_tables(void);
static void touch_symbol_table_member(void);
static void free_members(void);
static bool parse_numeric_field(const char *field, size_t field_len, int base, long long *out);
static char *parse_ar_name_field(const struct ar_hdr *hdr, bool *is_special);
static void store_be32(unsigned char *dst, uint32_t value);
static void store_be64(unsigned char *dst, uint64_t value);
static time_t archive_timestamp_for(time_t source_mtime);
static size_t member_position(const struct ar_memb *m);
static void list_insert_tail(struct ar_memb *node);
static bool list_insert_relative(struct ar_memb *node, const char *anchor_name, bool before);
static struct ar_memb *list_find_first(const char *name, struct ar_memb **prev_out);
static int parse_mode_field(const struct ar_memb *m, mode_t *out_mode);
static int parse_uid_field(const struct ar_memb *m, uid_t *out_uid);
static int parse_gid_field(const struct ar_memb *m, gid_t *out_gid);
static int parse_mtime_field(const struct ar_memb *m, time_t *out_time);
static bool name_has_path_components(const char *name);

int main(int argc, char *argv[])
{
    progname = get_basename(argv[0]);

    if (strstr(progname, "ranlib")) {
        bool touch_only = false;
        int argi = 1;

        while (argi < argc && argv[argi][0] == '-') {
            if (strcmp(argv[argi], "-t") == 0) {
                touch_only = true;
                argi++;
                continue;
            }
            usage();
        }
        if (argi >= argc) usage();
        operation = AR_TABLE;
        archive_name = argv[argi];
        if (access(archive_name, F_OK) != 0) err(1, "%s", archive_name);
        read_archive(archive_name);
        if (touch_only) touch_symbol_table_member();
        else ranlib();
        write_archive(archive_name);
        free_members();
        return 0;
    }

    if (argc < 2) usage();

    int argi = 1;
    while (argi < argc && strncmp(argv[argi], "--", 2) == 0) {
        if (strcmp(argv[argi], "--") == 0) {
            argi++;
            break;
        }
        if (strcmp(argv[argi], "--no-same-owner") == 0) {
            no_same_owner = true;
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--plugin") == 0) {
            argi++;
            if (argi < argc && argv[argi][0] != '-') {
                argi++;
            }
            continue;
        }
        if (strncmp(argv[argi], "--plugin=", 9) == 0) {
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--format") == 0) {
            const char *fmt;
            argi++;
            if (argi >= argc) {
                errx(1, "--format requires an argument");
            }
            fmt = argv[argi++];
            if (strcmp(fmt, "bsd") == 0) {
                archive_format = ARFMT_BSD;
            } else if (strcmp(fmt, "gnu") == 0) {
                archive_format = ARFMT_GNU;
            } else {
                errx(1, "unsupported --format value: %s", fmt);
            }
            format_forced = true;
            continue;
        }
        if (strncmp(argv[argi], "--format=", 9) == 0) {
            const char *fmt = argv[argi] + 9;
            if (strcmp(fmt, "bsd") == 0) {
                archive_format = ARFMT_BSD;
            } else if (strcmp(fmt, "gnu") == 0) {
                archive_format = ARFMT_GNU;
            } else {
                errx(1, "unsupported --format value: %s", fmt);
            }
            format_forced = true;
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--verbose") == 0) {
            modifiers |= MOD_VERBOSE;
            argi++;
            continue;
        }
        warnx("unknown option: %s", argv[argi]);
        usage();
    }

    if (argi >= argc) {
        usage();
    }

    char *key = argv[argi++];
    if (key[0] == '-') {
        key++;
    }

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
        case 'a': modifiers |= MOD_AFTER; break;
        case 'b':
        case 'i': modifiers |= MOD_BEFORE; break;
        case 'o': modifiers |= MOD_PRESERVE; break;
        case 'N': modifiers |= MOD_COUNT; break;
        case 'l': modifiers |= MOD_LOCAL; break;
        case 'C': modifiers |= MOD_NOCLOBBER; break;
        case 'U':
        case 'D': modifiers |= MOD_DETERMINISTIC; break;
        case 'S': modifiers |= MOD_NOSYMTAB; break;
        case 'T': modifiers |= MOD_THIN; break;
        default:
            warnx("illegal option -- %c", *key);
            usage();
        }
        key++;
    }

    if (modifiers & MOD_COUNT) {
        char *end = NULL;
        unsigned long parsed;
        if (argi >= argc) {
            errx(1, "modifier -N requires a positive count argument");
        }
        errno = 0;
        parsed = strtoul(argv[argi], &end, 10);
        if (errno != 0 || end == argv[argi] || *end != '\0' || parsed == 0) {
            errx(1, "invalid -N count: %s", argv[argi]);
        }
        instance_count = parsed;
        argi++;
    }

    if ((modifiers & MOD_AFTER) && (modifiers & MOD_BEFORE)) {
        errx(1, "only one of -a or -b/-i can be specified");
    }

    if ((modifiers & (MOD_AFTER | MOD_BEFORE)) != 0) {
        if (argi >= argc) {
            errx(1, "position modifier requires a member name");
        }
        position_member = argv[argi++];
    }

    if (argi >= argc) {
        usage();
    }
    archive_name = argv[argi++];

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
    if ((operation & AR_APPEND) && (modifiers & MOD_UPDATE)) {
        errx(1, "modifier -u is incompatible with -q");
    }

    if (operation != AR_APPEND && operation != AR_REPLACE) {
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
        if (modifiers & MOD_NOSYMTAB) drop_symbol_tables();
        else if (modifiers & AR_TABLE) ranlib();
        write_archive(archive_name);
        break;
    case AR_DELETE:
        delete_members(files, file_count);
        if (modifiers & MOD_NOSYMTAB) drop_symbol_tables();
        else if (modifiers & AR_TABLE) ranlib();
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
        if (modifiers & MOD_NOSYMTAB) drop_symbol_tables();
        else if (modifiers & AR_TABLE) ranlib();
        write_archive(archive_name);
        break;
    case AR_PRINT:
        print_members(files, file_count);
        break;
    case AR_TABLE:
        if (modifiers & MOD_NOSYMTAB) drop_symbol_tables();
        else ranlib();
        write_archive(archive_name);
        break;
    }

    free_members();
    return 0;
}

static void usage(void) {
    fprintf(stderr, "usage: ar [drqtpmx][lsvV] archive [member...]\n");
    exit(2);
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

static bool parse_numeric_field(const char *field, size_t field_len, int base, long long *out) {
    char tmp[64];
    char *end = NULL;
    long long value;

    if (out == NULL || field_len == 0 || field_len >= sizeof(tmp)) {
        return false;
    }

    memcpy(tmp, field, field_len);
    tmp[field_len] = '\0';

    errno = 0;
    value = strtoll(tmp, &end, base);
    if (errno != 0 || end == tmp) {
        return false;
    }

    while (*end == ' ') {
        end++;
    }
    if (*end != '\0') {
        return false;
    }

    *out = value;
    return true;
}

static void list_insert_tail(struct ar_memb *node) {
    struct ar_memb *cur;

    if (node == NULL) {
        return;
    }
    node->next = NULL;

    if (head == NULL) {
        head = node;
        return;
    }

    cur = head;
    while (cur->next != NULL) {
        cur = cur->next;
    }
    cur->next = node;
}

static bool list_insert_relative(struct ar_memb *node, const char *anchor_name, bool before) {
    struct ar_memb *anchor;
    struct ar_memb *anchor_prev = NULL;

    if (node == NULL || anchor_name == NULL || anchor_name[0] == '\0') {
        return false;
    }

    anchor = list_find_first(anchor_name, &anchor_prev);
    if (anchor == NULL) {
        return false;
    }

    if (before) {
        if (anchor_prev != NULL) {
            anchor_prev->next = node;
        } else {
            head = node;
        }
        node->next = anchor;
        return true;
    }

    node->next = anchor->next;
    anchor->next = node;
    return true;
}

static struct ar_memb *list_find_first(const char *name, struct ar_memb **prev_out) {
    struct ar_memb *cur = head;
    struct ar_memb *prev = NULL;

    while (cur != NULL) {
        if (!cur->deleted && strcmp(cur->name, name) == 0) {
            if (prev_out != NULL) {
                *prev_out = prev;
            }
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }

    if (prev_out != NULL) {
        *prev_out = NULL;
    }
    return NULL;
}

static int parse_mode_field(const struct ar_memb *m, mode_t *out_mode) {
    long long value;

    if (m == NULL || out_mode == NULL) {
        return -1;
    }
    if (!parse_numeric_field(m->hdr.ar_mode, sizeof(m->hdr.ar_mode), 8, &value) || value < 0) {
        return -1;
    }
    *out_mode = (mode_t)value;
    return 0;
}

static int parse_uid_field(const struct ar_memb *m, uid_t *out_uid) {
    long long value;

    if (m == NULL || out_uid == NULL) {
        return -1;
    }
    if (!parse_numeric_field(m->hdr.ar_uid, sizeof(m->hdr.ar_uid), 10, &value) || value < 0) {
        return -1;
    }
    *out_uid = (uid_t)value;
    return 0;
}

static int parse_gid_field(const struct ar_memb *m, gid_t *out_gid) {
    long long value;

    if (m == NULL || out_gid == NULL) {
        return -1;
    }
    if (!parse_numeric_field(m->hdr.ar_gid, sizeof(m->hdr.ar_gid), 10, &value) || value < 0) {
        return -1;
    }
    *out_gid = (gid_t)value;
    return 0;
}

static int parse_mtime_field(const struct ar_memb *m, time_t *out_time) {
    long long value;

    if (m == NULL || out_time == NULL) {
        return -1;
    }
    if (!parse_numeric_field(m->hdr.ar_date, sizeof(m->hdr.ar_date), 10, &value) || value < 0) {
        return -1;
    }
    *out_time = (time_t)value;
    return 0;
}

static bool name_has_path_components(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    if (strchr(name, '/') != NULL) {
        return true;
    }
    if (strstr(name, "..") != NULL) {
        return true;
    }
    return false;
}

static char *parse_ar_name_field(const struct ar_hdr *hdr, bool *is_special) {
    char tmp[17];
    char *name;
    char *slash;

    if (is_special != NULL) {
        *is_special = false;
    }

    memcpy(tmp, hdr->ar_name, 16);
    tmp[16] = '\0';

    for (int i = 15; i >= 0; i--) {
        if (tmp[i] == ' ') {
            tmp[i] = '\0';
        } else {
            break;
        }
    }

    if (archive_format == ARFMT_GNU) {
        if (strcmp(tmp, "/") == 0 || strcmp(tmp, "//") == 0 || strcmp(tmp, "/SYM64/") == 0) {
            if (is_special != NULL) {
                *is_special = true;
            }
            return strdup(tmp);
        }

        if (tmp[0] == '/' && isdigit((unsigned char)tmp[1])) {
            char *end = NULL;
            unsigned long off = strtoul(tmp + 1, &end, 10);
            if (end != NULL && *end == '\0' && gnu_name_table != NULL && off < gnu_name_table_size) {
                const char *start = gnu_name_table + off;
                const char *p = start;
                while ((size_t)(p - gnu_name_table) < gnu_name_table_size &&
                       *p != '\0' && *p != '\n') {
                    p++;
                }
                size_t nlen = (size_t)(p - start);
                if (nlen > 0 && start[nlen - 1] == '/') {
                    nlen--;
                }
                name = malloc(nlen + 1);
                if (name == NULL) {
                    return NULL;
                }
                memcpy(name, start, nlen);
                name[nlen] = '\0';
                return name;
            }
        }

        slash = strchr(tmp, '/');
        if (slash != NULL) {
            *slash = '\0';
        }
        return strdup(tmp);
    }

    slash = strchr(tmp, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    return strdup(tmp);
}

static void store_be32(unsigned char *dst, uint32_t value) {
    dst[0] = (unsigned char)((value >> 24) & 0xffu);
    dst[1] = (unsigned char)((value >> 16) & 0xffu);
    dst[2] = (unsigned char)((value >> 8) & 0xffu);
    dst[3] = (unsigned char)(value & 0xffu);
}

static void store_be64(unsigned char *dst, uint64_t value) {
    dst[0] = (unsigned char)((value >> 56) & 0xffu);
    dst[1] = (unsigned char)((value >> 48) & 0xffu);
    dst[2] = (unsigned char)((value >> 40) & 0xffu);
    dst[3] = (unsigned char)((value >> 32) & 0xffu);
    dst[4] = (unsigned char)((value >> 24) & 0xffu);
    dst[5] = (unsigned char)((value >> 16) & 0xffu);
    dst[6] = (unsigned char)((value >> 8) & 0xffu);
    dst[7] = (unsigned char)(value & 0xffu);
}

static time_t archive_timestamp_for(time_t source_mtime) {
    const char *zero_ar_date = getenv("ZERO_AR_DATE");
    const char *sde = getenv("SOURCE_DATE_EPOCH");
    char *end = NULL;
    long long v;

    if (zero_ar_date != NULL && zero_ar_date[0] != '\0') {
        return 0;
    }
    if (sde != NULL && sde[0] != '\0') {
        errno = 0;
        v = strtoll(sde, &end, 10);
        if (errno == 0 && end != sde && *end == '\0' && v >= 0) {
            return (time_t)v;
        }
    }
    if (modifiers & MOD_DETERMINISTIC) {
        return 0;
    }
    return source_mtime;
}

static size_t member_position(const struct ar_memb *m) {
    size_t pos = 0;
    const struct ar_memb *cur = head;
    while (cur != NULL) {
        if (cur == m) {
            return pos;
        }
        pos++;
        cur = cur->next;
    }
    return (size_t)-1;
}

static void read_archive(const char *path) {
    bool first_member = true;
    size_t member_count = 0;
    int fd = open(path, O_RDONLY);
    struct stat st_file;
    off_t archive_size = -1;
    if (fd < 0) {
        if (errno == ENOENT) return;
        err(1, "%s", path);
    }
    if (fstat(fd, &st_file) == 0) {
        archive_size = st_file.st_size;
    }

    char magic[8];
    if (read(fd, magic, 8) != 8) {
        errx(1, "%s: file format not recognized", path);
    }
    if (memcmp(magic, ARMAG, 8) == 0) {
        archive_is_thin = false;
    } else if (memcmp(magic, THINMAG, 8) == 0) {
        archive_is_thin = true;
    } else {
        errx(1, "%s: file format not recognized", path);
    }
    free(gnu_name_table);
    gnu_name_table = NULL;
    gnu_name_table_size = 0;
    if (!format_forced) {
        archive_format = ARFMT_GNU;
    }

    struct ar_hdr hdr;
    while (read(fd, &hdr, sizeof(hdr)) == sizeof(hdr)) {
        char raw_name[17];
        long long parsed_size = 0;

        if (memcmp(hdr.ar_fmag, ARFMAG, 2) != 0) {
            warnx("malformed header in %s", path);
            continue;
        }

        memcpy(raw_name, hdr.ar_name, 16);
        raw_name[16] = '\0';
        if (first_member && !format_forced) {
            if (memcmp(raw_name, "#1/", 3) == 0) {
                archive_format = ARFMT_BSD;
            } else if (raw_name[0] == '/' &&
                       (raw_name[1] == '/' || raw_name[1] == ' ' ||
                        isdigit((unsigned char)raw_name[1]))) {
                archive_format = ARFMT_GNU;
            }
            first_member = false;
            if (modifiers & MOD_VERBOSE) {
                warnx("%s: detected %s archive format",
                      path, archive_format == ARFMT_GNU ? "GNU" : "BSD");
            }
        }

        if (!parse_numeric_field(hdr.ar_size, sizeof(hdr.ar_size), 10, &parsed_size) ||
            parsed_size < 0) {
            warnx("%s: invalid member size field", path);
            continue;
        }
        if ((unsigned long long)parsed_size > (unsigned long long)SIZE_MAX) {
            warnx("%s: member size too large", path);
            break;
        }
        long size = (long)parsed_size;
        if (archive_size >= 0) {
            off_t cur_pos = lseek(fd, 0, SEEK_CUR);
            if (cur_pos >= 0 && cur_pos + (off_t)size > archive_size) {
                warnx("%s: truncated member data", path);
                break;
            }
        }
        if (!parse_numeric_field(hdr.ar_uid, sizeof(hdr.ar_uid), 10, &parsed_size)) {
            warnx("%s: non-numeric ar_uid field", path);
        }
        if (!parse_numeric_field(hdr.ar_gid, sizeof(hdr.ar_gid), 10, &parsed_size)) {
            warnx("%s: non-numeric ar_gid field", path);
        }
        if (!parse_numeric_field(hdr.ar_mode, sizeof(hdr.ar_mode), 8, &parsed_size)) {
            warnx("%s: non-numeric ar_mode field", path);
        }

        struct ar_memb *m = malloc(sizeof(struct ar_memb));
        if (m == NULL) {
            errx(1, "out of memory");
        }
        m->hdr = hdr;
        m->next = NULL;
        m->deleted = false;
        m->dirty = false;
        m->thin_ref = false;
        m->thin_path = NULL;
        m->gnu_name_ref = false;
        m->gnu_name_off = 0;

        if (memcmp(hdr.ar_name, "#1/", 3) == 0) {
            int name_len = atoi(hdr.ar_name + 3);
            int raw_name_len = name_len;
            m->name = malloc(name_len + 1);
            if (m->name == NULL) {
                errx(1, "out of memory");
            }
            if (read(fd, m->name, name_len) != name_len) {
                warnx("%s: truncated extended member name", path);
                free(m->name);
                free(m);
                break;
            }
            m->name[name_len] = 0;
            while (name_len > 0 && m->name[name_len - 1] == '\0') {
                name_len--;
            }
            m->name[name_len] = 0;
            size_t payload_size = size - raw_name_len;
            if (archive_is_thin &&
                strcmp(m->name, RANLIBMAG) != 0 &&
                strcmp(m->name, RANLIBSORT) != 0 &&
                strcmp(m->name, "/") != 0 &&
                strcmp(m->name, "//") != 0) {
                m->thin_ref = true;
                m->thin_path = malloc(payload_size + 1);
                if (m->thin_path == NULL) {
                    errx(1, "out of memory");
                }
                if (read(fd, m->thin_path, payload_size) != (ssize_t)payload_size) {
                    warnx("%s: truncated thin member path", path);
                    free(m->thin_path);
                    free(m->name);
                    free(m);
                    break;
                }
                m->thin_path[payload_size] = 0;
                m->size = 0;
                m->data = NULL;
            } else {
                m->size = payload_size;
                m->data = malloc(m->size);
                if (m->data == NULL && m->size != 0) {
                    errx(1, "out of memory");
                }
                if (m->size > 0 && read(fd, m->data, m->size) != (ssize_t)m->size) {
                    warnx("%s: truncated member payload", path);
                    free(m->data);
                    free(m->name);
                    free(m);
                    break;
                }
            }
        } else {
            bool special = false;
            m->name = parse_ar_name_field(&hdr, &special);
            (void)special;
            if (m->name == NULL) {
                errx(1, "out of memory while parsing member name");
            }
            if (archive_is_thin &&
                strcmp(m->name, RANLIBMAG) != 0 &&
                strcmp(m->name, RANLIBSORT) != 0 &&
                strcmp(m->name, "/") != 0 &&
                strcmp(m->name, "//") != 0) {
                m->thin_ref = true;
                m->thin_path = malloc(size + 1);
                if (m->thin_path == NULL) {
                    errx(1, "out of memory");
                }
                if (read(fd, m->thin_path, size) != size) {
                    warnx("%s: truncated thin member path", path);
                    free(m->thin_path);
                    free(m->name);
                    free(m);
                    break;
                }
                m->thin_path[size] = 0;
                m->size = 0;
                m->data = NULL;
            } else {
                m->size = size;
                m->data = malloc(size);
                if (m->data == NULL && size != 0) {
                    errx(1, "out of memory");
                }
                if (size > 0 && read(fd, m->data, size) != size) {
                    warnx("%s: truncated member payload", path);
                    free(m->data);
                    free(m->name);
                    free(m);
                    break;
                }

                if (archive_format == ARFMT_GNU && strcmp(m->name, "//") == 0) {
                    free(gnu_name_table);
                    gnu_name_table = malloc(m->size + 1);
                    if (gnu_name_table != NULL) {
                        memcpy(gnu_name_table, m->data, m->size);
                        gnu_name_table[m->size] = '\0';
                        gnu_name_table_size = m->size;
                    } else {
                        gnu_name_table_size = 0;
                    }
                }
            }
        }

        if (size % 2 != 0) lseek(fd, 1, SEEK_CUR);

        if (head == NULL) head = m;
        else {
            struct ar_memb *cur = head;
            while (cur->next) cur = cur->next;
            cur->next = m;
        }

        member_count++;
        if (member_count > 1000000) {
            warnx("%s: refusing archive with too many members", path);
            break;
        }
    }

    struct ar_memb *cur = head;
    time_t sym_mtime = 0;
    time_t max_member_mtime = 0;
    bool have_sym = false;
    while (cur != NULL) {
        time_t mt = 0;
        if (parse_mtime_field(cur, &mt) == 0) {
            if (strcmp(cur->name, RANLIBMAG) == 0 ||
                strcmp(cur->name, RANLIBSORT) == 0 ||
                strcmp(cur->name, "/") == 0 ||
                strcmp(cur->name, "/SYM64/") == 0) {
                sym_mtime = mt;
                have_sym = true;
            } else if (mt > max_member_mtime) {
                max_member_mtime = mt;
            }
        }
        cur = cur->next;
    }
    if (have_sym && max_member_mtime > sym_mtime) {
        warnx("%s: stale symbol table", path);
    }

    close(fd);
}

static void append_files(char **files, int count) {
    for (int i = 0; i < count; i++) {
        const char *fname = files[i];
        struct ar_memb *m = NULL;
        struct ar_memb *m_prev = NULL;
        bool thin_mode = archive_is_thin || ((modifiers & MOD_THIN) != 0);
        void *data = NULL;
        int fd = open(fname, O_RDONLY);
        if (fd < 0) {
            warn("%s", fname);
            continue;
        }

        struct stat st;
        if (fstat(fd, &st) != 0) {
            warn("%s", fname);
            close(fd);
            continue;
        }

        if (!thin_mode) {
            data = malloc(st.st_size);
            if (data == NULL && st.st_size != 0) {
                warnx("%s: out of memory", fname);
                close(fd);
                continue;
            }
            if (st.st_size > 0 && read(fd, data, st.st_size) != st.st_size) {
                warn("%s", fname);
                free(data);
                close(fd);
                continue;
            }
        }
        close(fd);

        const char *base = get_basename(fname);
        if (operation == AR_REPLACE) {
            m = list_find_first(base, &m_prev);
        }

        if (operation == AR_REPLACE && m) {
            if (modifiers & MOD_UPDATE) {
                time_t old_time = 0;
                if (parse_mtime_field(m, &old_time) == 0 && st.st_mtime <= old_time) {
                    free(data);
                    continue;
                }
            }
            if (modifiers & MOD_VERBOSE) printf("r - %s\n", base);
            free(m->data);
            free(m->thin_path);
            free(m->name);
        } else {
            if (modifiers & MOD_VERBOSE) printf("a - %s\n", base);
            m = malloc(sizeof(struct ar_memb));
            if (m == NULL) {
                warnx("%s: out of memory", fname);
                free(data);
                continue;
            }
            m->next = NULL;
            if ((modifiers & (MOD_AFTER | MOD_BEFORE)) != 0 && position_member != NULL) {
                bool inserted = list_insert_relative(m, position_member, (modifiers & MOD_BEFORE) != 0);
                if (!inserted) {
                    warnx("%s: position member '%s' not found, appending", base, position_member);
                    list_insert_tail(m);
                }
            } else {
                list_insert_tail(m);
            }
        }

        m->data = thin_mode ? NULL : data;
        m->size = (size_t)st.st_size;
        m->name = strdup(base);
        m->thin_ref = thin_mode;
        m->thin_path = thin_mode ? strdup(fname) : NULL;
        if (thin_mode && m->thin_path == NULL) {
            warnx("%s: out of memory", fname);
            if (m->data != NULL) free(m->data);
            m->data = NULL;
            m->size = 0;
            m->thin_ref = false;
            continue;
        }
        m->dirty = true;
        m->deleted = false;
        m->gnu_name_ref = false;
        m->gnu_name_off = 0;

        memset(&m->hdr, ' ', sizeof(struct ar_hdr));
        char buf[32];
        snprintf(buf, sizeof(buf), "%-12ld", (long)archive_timestamp_for(st.st_mtime));
        memcpy(m->hdr.ar_date, buf, 12);
        snprintf(buf, sizeof(buf), "%-6d", (int)((modifiers & MOD_DETERMINISTIC) ? 0 : st.st_uid));
        memcpy(m->hdr.ar_uid, buf, 6);
        snprintf(buf, sizeof(buf), "%-6d", (int)((modifiers & MOD_DETERMINISTIC) ? 0 : st.st_gid));
        memcpy(m->hdr.ar_gid, buf, 6);
        snprintf(buf, sizeof(buf), "%-8o", (int)((modifiers & MOD_DETERMINISTIC) ? 0644 : st.st_mode));
        memcpy(m->hdr.ar_mode, buf, 8);
        size_t payload_size = thin_mode ? strlen(m->thin_path) : (size_t)st.st_size;
        snprintf(buf, sizeof(buf), "%-10ld", (long)payload_size);
        memcpy(m->hdr.ar_size, buf, 10);
        memcpy(m->hdr.ar_fmag, ARFMAG, 2);
        archive_is_thin = thin_mode;
        (void)m_prev;
    }
}

static void delete_members(char **members, int count) {
    if (count == 0) return;
    for (int i = 0; i < count; i++) {
        struct ar_memb *cur = head;
        bool found = false;
        unsigned long seen = 0;
        while (cur) {
            if (!cur->deleted && strcmp(cur->name, members[i]) == 0) {
                if (modifiers & MOD_COUNT) {
                    seen++;
                    if (seen != instance_count) {
                        cur = cur->next;
                        continue;
                    }
                }
                if (modifiers & MOD_VERBOSE) printf("d - %s\n", cur->name);
                cur->deleted = true;
                found = true;
                if (modifiers & MOD_COUNT) {
                    break;
                }
            }
            cur = cur->next;
        }
        if (!found) warnx("%s: not found in archive", members[i]);
    }
}

static void move_members(char **members, int count) {
    bool use_relative = (modifiers & (MOD_AFTER | MOD_BEFORE)) != 0;

    if (count == 0) return;
    if (use_relative && (position_member == NULL || position_member[0] == '\0')) {
        errx(1, "move with -a/-b/-i requires a position member");
    }
    if (use_relative && list_find_first(position_member, NULL) == NULL) {
        errx(1, "position member '%s' not found", position_member);
    }

    for (int i = 0; i < count; i++) {
        struct ar_memb *prev = NULL;
        struct ar_memb *target = list_find_first(members[i], &prev);

        if (target == NULL) {
            warnx("%s: not found", members[i]);
            continue;
        }

        if (use_relative && strcmp(target->name, position_member) == 0) {
            continue;
        }

        if (prev != NULL) {
            prev->next = target->next;
        } else {
            head = target->next;
        }

        if (modifiers & MOD_VERBOSE) printf("m - %s\n", target->name);
        target->next = NULL;

        if (use_relative) {
            if (!list_insert_relative(target, position_member, (modifiers & MOD_BEFORE) != 0)) {
                errx(1, "position member '%s' not found", position_member);
            }
        } else {
            list_insert_tail(target);
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
            if (cur->thin_ref && cur->thin_path != NULL) {
                int fd = open(cur->thin_path, O_RDONLY);
                if (fd < 0) {
                    warn("%s", cur->thin_path);
                } else {
                    char buf[4096];
                    ssize_t nr;
                    while ((nr = read(fd, buf, sizeof(buf))) > 0) {
                        if (write(STDOUT_FILENO, buf, (size_t)nr) != nr) {
                            warn("stdout");
                            break;
                        }
                    }
                    close(fd);
                }
            } else {
                fwrite(cur->data, cur->size, 1, stdout);
            }
        }
        cur = cur->next;
    }
}

static void extract_members(char **members, int count) {
    unsigned long *seen = NULL;
    struct ar_memb *cur = head;

    if ((modifiers & MOD_COUNT) && count > 0) {
        seen = calloc((size_t)count, sizeof(*seen));
        if (seen == NULL) {
            errx(1, "out of memory");
        }
    }

    while (cur) {
        if (cur->deleted) { cur = cur->next; continue; }
        bool match = (count == 0);
        for (int i = 0; i < count; i++) {
            if (strcmp(cur->name, members[i]) == 0) {
                if ((modifiers & MOD_COUNT) && seen != NULL) {
                    seen[i]++;
                    if (seen[i] == instance_count) {
                        match = true;
                    }
                } else {
                    match = true;
                }
                break;
            }
        }
        if (match) {
            mode_t mode = 0644;
            uid_t uid = 0;
            gid_t gid = 0;
            time_t mtime = 0;
            const char *name = cur->name;

            if (name_has_path_components(name)) {
                warnx("%s: refusing to extract member with path components", name);
                cur = cur->next;
                continue;
            }

            if (parse_mode_field(cur, &mode) != 0) {
                mode = 0644;
            }

            if (modifiers & MOD_VERBOSE) printf("x - %s\n", name);
            int oflags = O_WRONLY | O_CREAT;
            if (modifiers & MOD_NOCLOBBER) {
                oflags |= O_EXCL;
            } else {
                oflags |= O_TRUNC;
            }
            int fd = open(name, oflags, mode & 07777);
            if (fd < 0) warn("%s", name);
            else {
                if (cur->thin_ref && cur->thin_path != NULL) {
                    int srcfd = open(cur->thin_path, O_RDONLY);
                    if (srcfd < 0) {
                        warn("%s", cur->thin_path);
                    } else {
                        char buf[4096];
                        ssize_t nr;
                        while ((nr = read(srcfd, buf, sizeof(buf))) > 0) {
                            if (write(fd, buf, (size_t)nr) != nr) {
                                warn("%s", name);
                                break;
                            }
                        }
                        close(srcfd);
                    }
                } else {
                    ssize_t wr = write(fd, cur->data, cur->size);
                    if (wr != (ssize_t)cur->size) {
                        warn("%s", name);
                    }
                }
                if (fchmod(fd, mode & 07777) != 0) {
                    warn("%s", name);
                }
                close(fd);

                if (!no_same_owner && geteuid() == 0 &&
                    parse_uid_field(cur, &uid) == 0 &&
                    parse_gid_field(cur, &gid) == 0) {
                    if (chown(name, uid, gid) != 0) {
                        warn("%s", name);
                    }
                }

                if ((modifiers & MOD_PRESERVE) && parse_mtime_field(cur, &mtime) == 0) {
                    struct timeval tv[2];
                    tv[0].tv_sec = mtime;
                    tv[0].tv_usec = 0;
                    tv[1] = tv[0];
                    if (utimes(name, tv) != 0) {
                        warn("%s", name);
                    }
                }
            }
        }
        cur = cur->next;
    }

    free(seen);
}

static void list_members(char **members, int count) {
    unsigned long *seen = NULL;
    struct ar_memb *cur = head;

    if ((modifiers & MOD_COUNT) && count > 0) {
        seen = calloc((size_t)count, sizeof(*seen));
        if (seen == NULL) {
            errx(1, "out of memory");
        }
    }

    while (cur) {
        if (cur->deleted) { cur = cur->next; continue; }
        if (strcmp(cur->name, RANLIBMAG) == 0 || strcmp(cur->name, RANLIBSORT) == 0 ||
            strcmp(cur->name, "/") == 0 || strcmp(cur->name, "/SYM64/") == 0 ||
            strcmp(cur->name, "//") == 0) {
            cur = cur->next; continue;
        }
        bool match = (count == 0);
        for (int i = 0; i < count; i++) {
            if (strcmp(cur->name, members[i]) == 0) {
                if ((modifiers & MOD_COUNT) && seen != NULL) {
                    seen[i]++;
                    if (seen[i] == instance_count) {
                        match = true;
                    }
                } else {
                    match = true;
                }
                break;
            }
        }
        if (match) {
            if (modifiers & MOD_VERBOSE) {
                mode_t mode = 0;
                uid_t uid = 0;
                gid_t gid = 0;
                time_t mtime = 0;
                long display_size = (long)cur->size;
                char date_buf[32] = "Jan  1 00:00 1970";
                char type_char = '-';
                struct stat stbuf;

                (void)parse_mode_field(cur, &mode);
                (void)parse_uid_field(cur, &uid);
                (void)parse_gid_field(cur, &gid);

                if (parse_mtime_field(cur, &mtime) == 0) {
                    struct tm *tm = localtime(&mtime);
                    if (tm != NULL) {
                        (void)strftime(date_buf, sizeof(date_buf), "%b %e %H:%M %Y", tm);
                    }
                }

                if (S_ISDIR(mode)) type_char = 'd';
                else if (S_ISCHR(mode)) type_char = 'c';
                else if (S_ISBLK(mode)) type_char = 'b';
                else if (S_ISFIFO(mode)) type_char = 'p';
                else if (S_ISLNK(mode)) type_char = 'l';
#ifdef S_ISSOCK
                else if (S_ISSOCK(mode)) type_char = 's';
#endif

                if (cur->thin_ref && cur->thin_path != NULL &&
                    stat(cur->thin_path, &stbuf) == 0) {
                    display_size = (long)stbuf.st_size;
                }

                printf("%c%c%c%c%c%c%c%c%c%c %ld/%ld %6ld %s %s\n",
                    type_char,
                    (mode & S_IRUSR) ? 'r' : '-', (mode & S_IWUSR) ? 'w' : '-', (mode & S_IXUSR) ? 'x' : '-',
                    (mode & S_IRGRP) ? 'r' : '-', (mode & S_IWGRP) ? 'w' : '-', (mode & S_IXGRP) ? 'x' : '-',
                    (mode & S_IROTH) ? 'r' : '-', (mode & S_IWOTH) ? 'w' : '-', (mode & S_IXOTH) ? 'x' : '-',
                    (long)uid, (long)gid, display_size, date_buf, cur->name);
            } else {
                printf("%s\n", cur->name);
            }
        }
        cur = cur->next;
    }

    free(seen);
}

static void get_elf_symbols(struct ar_memb *m, struct sym_entry **sym_head) {
    elfobj_t *obj = NULL;
    elf_err_t open_err;
    size_t symbol_count;

    if (m == NULL || m->thin_ref || m->data == NULL || m->size == 0) {
        return;
    }

    /*
     * Non-ELF archive members are legal. If parsing fails, skip symbol
     * extraction without failing the archive operation.
     */
    open_err = elf_open_memory_with_options(m->data, m->size, ELFOBJ_OPEN_NOCOPY, &obj);
    if (open_err != ELF_OK || obj == NULL) {
        return;
    }

    symbol_count = elf_symbol_count(obj);
    for (size_t i = 0; i < symbol_count; i++) {
        elf_symbol_t *symbol;
        const char *name;
        uint8_t bind;
        uint8_t type;
        uint16_t shndx;

        symbol = elf_symbol_at(obj, i);
        if (symbol == NULL) {
            continue;
        }

        bind = elf_symbol_bind(symbol);
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }
        type = elf_symbol_type(symbol);
        if (type == STT_FILE || type == STT_SECTION) {
            continue;
        }

        shndx = elf_symbol_shndx(symbol);
        if (shndx == SHN_UNDEF) {
            continue;
        }

        name = elf_symbol_name(symbol);
        if (name == NULL || name[0] == '\0') {
            continue;
        }

        struct sym_entry *s = malloc(sizeof(struct sym_entry));
        if (s == NULL) {
            continue;
        }

        s->name = strdup(name);
        if (s->name == NULL) {
            free(s);
            continue;
        }

        s->offset = 0;
        s->member = m;
        s->next = *sym_head;
        *sym_head = s;
    }

    elf_close(obj);
}

static int compare_syms(const void *a, const void *b) {
    struct sym_entry *const *sa = a;
    struct sym_entry *const *sb = b;
    int cmp = strcmp((*sa)->name, (*sb)->name);
    if (cmp != 0) {
        return cmp;
    }
    size_t posa = member_position((*sa)->member);
    size_t posb = member_position((*sb)->member);
    if (posa < posb) return -1;
    if (posa > posb) return 1;
    return 0;
}

static void drop_symbol_tables(void) {
    struct ar_memb *cur = head;
    struct ar_memb *prev = NULL;

    while (cur != NULL) {
        if (strcmp(cur->name, RANLIBMAG) == 0 || strcmp(cur->name, RANLIBSORT) == 0 ||
            strcmp(cur->name, "/") == 0 || strcmp(cur->name, "/SYM64/") == 0 ||
            strcmp(cur->name, "//") == 0) {
            struct ar_memb *next = cur->next;
            if (prev != NULL) {
                prev->next = next;
            } else {
                head = next;
            }
            free(cur->name);
            free(cur->data);
            free(cur->thin_path);
            free(cur);
            cur = next;
            continue;
        }
        prev = cur;
        cur = cur->next;
    }
}

static void free_members(void) {
    struct ar_memb *cur = head;
    while (cur != NULL) {
        struct ar_memb *next = cur->next;
        free(cur->name);
        free(cur->data);
        free(cur->thin_path);
        free(cur);
        cur = next;
    }
    head = NULL;
}

static void touch_symbol_table_member(void) {
    struct ar_memb *cur = head;
    while (cur != NULL) {
        if (strcmp(cur->name, RANLIBMAG) == 0 ||
            strcmp(cur->name, RANLIBSORT) == 0 ||
            strcmp(cur->name, "/") == 0 ||
            strcmp(cur->name, "/SYM64/") == 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%-12ld", (long)time(NULL));
            memcpy(cur->hdr.ar_date, buf, 12);
            cur->dirty = true;
            return;
        }
        cur = cur->next;
    }
}

static void ranlib(void) {
    struct ar_memb *cur = head;
    drop_symbol_tables();
    if (archive_is_thin || (modifiers & MOD_THIN)) {
        return;
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

    size_t total_size;
    if (archive_format == ARFMT_GNU) {
        /* Reserve for potential /SYM64/ payload (count + 64-bit offsets + strings). */
        total_size = sizeof(uint64_t) + (size_t)count * sizeof(uint64_t) + strsize;
    } else {
        uint32_t array_size = count * sizeof(struct ranlib);
        total_size = sizeof(uint32_t) + array_size + sizeof(uint32_t) + strsize;
    }

    /* Create dummy data to reserve space */
    void *data = calloc(1, total_size);
    /* We'll fill offsets in write_archive */

    struct ar_memb *m = malloc(sizeof(struct ar_memb));
    m->name = strdup(archive_format == ARFMT_GNU ? "/" : RANLIBSORT);
    m->data = data;
    m->size = total_size;
    m->thin_path = NULL;
    m->deleted = false;
    m->dirty = true;
    m->thin_ref = false;
    m->gnu_name_ref = false;
    m->gnu_name_off = 0;
    memset(&m->hdr, ' ', sizeof(struct ar_hdr));
    char buf[32];
    snprintf(buf, sizeof(buf), "%-12ld", (long)archive_timestamp_for(time(NULL)));
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
    snprintf(buf, sizeof(buf), "%-16s", m->name);
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
    char *gnu_table = NULL;
    size_t gnu_table_len = 0;
    char tmp_path[1024];
    FILE *fp;
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", path, (long)getpid());
    fp = fopen(tmp_path, "w");
    if (!fp) err(1, "fopen %s", tmp_path);
    bool thin_output = archive_is_thin || ((modifiers & MOD_THIN) != 0);
    archive_is_thin = thin_output;

    if (archive_format == ARFMT_GNU) {
        struct ar_memb *m = head;
        while (m != NULL) {
            m->gnu_name_ref = false;
            m->gnu_name_off = 0;
            if (!m->deleted &&
                strcmp(m->name, "/") != 0 &&
                strcmp(m->name, "/SYM64/") != 0 &&
                strcmp(m->name, "//") != 0 &&
                strcmp(m->name, RANLIBMAG) != 0 &&
                strcmp(m->name, RANLIBSORT) != 0) {
                size_t nlen = strlen(m->name);
                if (nlen > 15 || strchr(m->name, ' ') != NULL) {
                    size_t entry_len = nlen + 2; /* name + '/' + '\n' */
                    char *next = realloc(gnu_table, gnu_table_len + entry_len);
                    if (next == NULL) {
                        free(gnu_table);
                        errx(1, "out of memory building GNU longname table");
                    }
                    gnu_table = next;
                    memcpy(gnu_table + gnu_table_len, m->name, nlen);
                    gnu_table[gnu_table_len + nlen] = '/';
                    gnu_table[gnu_table_len + nlen + 1] = '\n';
                    m->gnu_name_ref = true;
                    m->gnu_name_off = gnu_table_len;
                    gnu_table_len += entry_len;
                }
            }
            m = m->next;
        }
    }

    fprintf(fp, "%s", thin_output ? THINMAG : ARMAG);
    uint32_t offset = 8;

    struct ar_memb *symdef = NULL;
    if (head &&
        (strcmp(head->name, RANLIBMAG) == 0 ||
         strcmp(head->name, "/") == 0 ||
         strcmp(head->name, "/SYM64/") == 0)) {
        symdef = head;
    }

    struct ar_memb *cur = head;

    /* Pre-calc offsets for symbol table */
    if (symdef) {
        /* Need to rebuild sorted symbol table */
        struct sym_entry *list = NULL;
        int count = 0;

        /* Calculate initial offset past symdef */
        uint64_t current_off = offset + sizeof(struct ar_hdr) + symdef->size + (symdef->size % 2);
        if (archive_format == ARFMT_GNU && gnu_table_len > 0) {
            current_off += sizeof(struct ar_hdr) + gnu_table_len + (gnu_table_len % 2);
        }

        struct ar_memb *m = symdef->next;
        while (m) {
            if (!m->deleted &&
                strcmp(m->name, "//") != 0 &&
                strcmp(m->name, "/") != 0 &&
                strcmp(m->name, "/SYM64/") != 0 &&
                strcmp(m->name, RANLIBMAG) != 0 &&
                strcmp(m->name, RANLIBSORT) != 0) {
                uint64_t m_off = current_off;

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
                size_t payload_size = m->thin_ref && m->thin_path != NULL ? strlen(m->thin_path) : m->size;
                bool extended = (name_len > 15 || strchr(m->name, ' '));
                long m_size = (long)payload_size;
                if (archive_format != ARFMT_GNU && extended) {
                    m_size += name_len;
                }
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
            int strsize = 0;
            for (int i = 0; i < count; i++) strsize += strlen(arr[i]->name) + 1;
            if (archive_format == ARFMT_GNU) {
                uint64_t max_off = 0;
                for (int i = 0; i < count; i++) {
                    if (arr[i]->offset > max_off) {
                        max_off = arr[i]->offset;
                    }
                }
                if (max_off > 0xffffffffULL) {
                    size_t total = sizeof(uint64_t) + (size_t)count * sizeof(uint64_t) + strsize;
                    unsigned char *p;
                    unsigned char *strp;
                    int stroff = 0;

                    symdef->data = realloc(symdef->data, total);
                    if (symdef->data == NULL) {
                        errx(1, "out of memory building GNU /SYM64/ table");
                    }
                    symdef->size = total;
                    free(symdef->name);
                    symdef->name = strdup("/SYM64/");
                    p = (unsigned char *)symdef->data;
                    store_be64(p, (uint64_t)count);
                    p += sizeof(uint64_t);
                    for (int i = 0; i < count; i++) {
                        store_be64(p, arr[i]->offset);
                        p += sizeof(uint64_t);
                    }
                    strp = p;
                    for (int i = 0; i < count; i++) {
                        strcpy((char *)strp + stroff, arr[i]->name);
                        stroff += strlen(arr[i]->name) + 1;
                    }
                } else {
                    size_t total = sizeof(uint32_t) + (size_t)count * sizeof(uint32_t) + strsize;
                    unsigned char *p;
                    unsigned char *strp;
                    int stroff = 0;

                    symdef->data = realloc(symdef->data, total);
                    if (symdef->data == NULL) {
                        errx(1, "out of memory building GNU symbol table");
                    }
                    symdef->size = total;
                    free(symdef->name);
                    symdef->name = strdup("/");
                    p = (unsigned char *)symdef->data;
                    store_be32(p, (uint32_t)count);
                    p += sizeof(uint32_t);
                    for (int i = 0; i < count; i++) {
                        store_be32(p, (uint32_t)arr[i]->offset);
                        p += sizeof(uint32_t);
                    }
                    strp = p;
                    for (int i = 0; i < count; i++) {
                        strcpy((char *)strp + stroff, arr[i]->name);
                        stroff += strlen(arr[i]->name) + 1;
                    }
                }
            } else {
                char *p = (char *)symdef->data;
                uint32_t array_size = count * sizeof(struct ranlib);
                *(uint32_t *)p = array_size;
                p += 4;
                struct ranlib *ra = (struct ranlib *)p;
                p += array_size;
                *(uint32_t *)p = strsize;
                p += 4;
                char *strp = p;

                int stroff = 0;
                for (int i = 0; i < count; i++) {
                    ra[i].ran_un.ran_strx = stroff;
                    ra[i].ran_off = (uint32_t)arr[i]->offset;
                    strcpy(strp + stroff, arr[i]->name);
                    stroff += strlen(arr[i]->name) + 1;
                }
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
    bool wrote_gnu_table = (archive_format != ARFMT_GNU || gnu_table_len == 0);
    while (cur) {
        if (cur->deleted) { cur = cur->next; continue; }

        if (archive_format == ARFMT_GNU && strcmp(cur->name, "//") == 0) {
            cur = cur->next;
            continue;
        }

        {
            char dbuf[32];
            time_t cur_mtime = 0;
            if (parse_mtime_field(cur, &cur_mtime) != 0) {
                cur_mtime = time(NULL);
            }
            snprintf(dbuf, sizeof(dbuf), "%-12ld", (long)archive_timestamp_for(cur_mtime));
            memcpy(cur->hdr.ar_date, dbuf, 12);
        }

        if (modifiers & MOD_DETERMINISTIC) {
            char dbuf[32];
            snprintf(dbuf, sizeof(dbuf), "%-6d", 0);
            memcpy(cur->hdr.ar_uid, dbuf, 6);
            snprintf(dbuf, sizeof(dbuf), "%-6d", 0);
            memcpy(cur->hdr.ar_gid, dbuf, 6);
            snprintf(dbuf, sizeof(dbuf), "%-8o", 0644);
            memcpy(cur->hdr.ar_mode, dbuf, 8);
        }

        if (!wrote_gnu_table &&
            strcmp(cur->name, "/") != 0 &&
            strcmp(cur->name, "/SYM64/") != 0 &&
            strcmp(cur->name, RANLIBMAG) != 0 &&
            strcmp(cur->name, RANLIBSORT) != 0) {
            struct ar_hdr gnu_hdr;
            char buf[32];

            memset(&gnu_hdr, ' ', sizeof(gnu_hdr));
            snprintf(buf, sizeof(buf), "%-16s", "//");
            memcpy(gnu_hdr.ar_name, buf, 16);
            snprintf(buf, sizeof(buf), "%-12ld",
                     (long)archive_timestamp_for(time(NULL)));
            memcpy(gnu_hdr.ar_date, buf, 12);
            snprintf(buf, sizeof(buf), "%-6d", 0);
            memcpy(gnu_hdr.ar_uid, buf, 6);
            snprintf(buf, sizeof(buf), "%-6d", 0);
            memcpy(gnu_hdr.ar_gid, buf, 6);
            snprintf(buf, sizeof(buf), "%-8o", 0644);
            memcpy(gnu_hdr.ar_mode, buf, 8);
            snprintf(buf, sizeof(buf), "%-10ld", (long)gnu_table_len);
            memcpy(gnu_hdr.ar_size, buf, 10);
            memcpy(gnu_hdr.ar_fmag, ARFMAG, 2);

            fwrite(&gnu_hdr, sizeof(gnu_hdr), 1, fp);
            fwrite(gnu_table, gnu_table_len, 1, fp);
            if ((gnu_table_len % 2) != 0) {
                fputc('\n', fp);
            }
            wrote_gnu_table = true;
        }

        int name_len = strlen(cur->name);
        size_t payload_size = cur->thin_ref && cur->thin_path != NULL ? strlen(cur->thin_path) : cur->size;
        bool extended = (name_len > 15 || strchr(cur->name, ' '));
        bool gnu_special = strcmp(cur->name, "/") == 0 || strcmp(cur->name, "/SYM64/") == 0;

        if (archive_format == ARFMT_GNU) {
            char buf[32];
            char namebuf[32];
            snprintf(buf, sizeof(buf), "%-10ld", (long)payload_size);
            memcpy(cur->hdr.ar_size, buf, 10);
            if (gnu_special) {
                snprintf(namebuf, sizeof(namebuf), "%s", cur->name);
            } else if (cur->gnu_name_ref) {
                snprintf(namebuf, sizeof(namebuf), "/%zu", cur->gnu_name_off);
            } else {
                snprintf(namebuf, sizeof(namebuf), "%s/", cur->name);
            }
            snprintf(buf, sizeof(buf), "%-16s", namebuf);
            memcpy(cur->hdr.ar_name, buf, 16);
        } else if (extended) {
            long data_len = (long)payload_size + name_len;
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
            snprintf(buf, sizeof(buf), "%-10ld", (long)payload_size);
            memcpy(cur->hdr.ar_size, buf, 10);
        }

        fwrite(&cur->hdr, sizeof(struct ar_hdr), 1, fp);
        if (archive_format != ARFMT_GNU && extended) fwrite(cur->name, name_len, 1, fp);
        if (cur->thin_ref && cur->thin_path != NULL) {
            fwrite(cur->thin_path, payload_size, 1, fp);
        } else {
            fwrite(cur->data, cur->size, 1, fp);
        }

        long total_data = (long)payload_size +
                          ((archive_format != ARFMT_GNU && extended) ? name_len : 0);
        if (total_data % 2 != 0) fputc('\n', fp);

        cur = cur->next;
    }
    free(gnu_table);
    if (fclose(fp) != 0) {
        unlink(tmp_path);
        err(1, "fclose %s", tmp_path);
    }
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        err(1, "rename %s", path);
    }
}
