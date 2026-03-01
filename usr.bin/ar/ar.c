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

static char *archive_name;
static int operation = 0;
static int modifiers = 0;
static const char *progname;
static const char *position_member = NULL;
static bool no_same_owner = false;

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
static bool parse_numeric_field(const char *field, size_t field_len, int base, long long *out);
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
        default:
            warnx("illegal option -- %c", *key);
            usage();
        }
        key++;
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
        struct ar_memb *m = NULL;
        struct ar_memb *m_prev = NULL;
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

        void *data = malloc(st.st_size);
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
        (void)m_prev;
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
            int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
            if (fd < 0) warn("%s", name);
            else {
                ssize_t wr = write(fd, cur->data, cur->size);
                if (wr != (ssize_t)cur->size) {
                    warn("%s", name);
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
                mode_t mode = 0;
                uid_t uid = 0;
                gid_t gid = 0;
                time_t mtime = 0;
                char date_buf[32] = "Jan  1 00:00 1970";
                char type_char = '-';

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

                printf("%c%c%c%c%c%c%c%c%c%c %ld/%ld %6ld %s %s\n",
                    type_char,
                    (mode & S_IRUSR) ? 'r' : '-', (mode & S_IWUSR) ? 'w' : '-', (mode & S_IXUSR) ? 'x' : '-',
                    (mode & S_IRGRP) ? 'r' : '-', (mode & S_IWGRP) ? 'w' : '-', (mode & S_IXGRP) ? 'x' : '-',
                    (mode & S_IROTH) ? 'r' : '-', (mode & S_IWOTH) ? 'w' : '-', (mode & S_IXOTH) ? 'x' : '-',
                    (long)uid, (long)gid, (long)cur->size, date_buf, cur->name);
            } else {
                printf("%s\n", cur->name);
            }
        }
        cur = cur->next;
    }
}

static void get_elf_symbols(struct ar_memb *m, struct sym_entry **sym_head) {
    elfobj_t *obj = NULL;
    elf_err_t open_err;
    size_t symbol_count;

    if (m == NULL || m->data == NULL || m->size == 0) {
        return;
    }

    /*
     * Non-ELF archive members are legal. If parsing fails, skip symbol
     * extraction without failing the archive operation.
     */
    open_err = elf_open_memory(m->data, m->size, &obj);
    if (open_err != ELF_OK || obj == NULL) {
        return;
    }

    symbol_count = elf_symbol_count(obj);
    for (size_t i = 0; i < symbol_count; i++) {
        elf_symbol_t *symbol;
        const char *name;
        uint8_t bind;
        uint16_t shndx;

        symbol = elf_symbol_at(obj, i);
        if (symbol == NULL) {
            continue;
        }

        bind = elf_symbol_bind(symbol);
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
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
