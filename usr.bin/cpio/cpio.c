#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <grp.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define IO_BUFSZ 65536

typedef enum { MODE_NONE, MODE_OUT, MODE_IN, MODE_PASS } mode_tn;
typedef enum { FMT_NEWC, FMT_ODC, FMT_BIN } fmt_t;

typedef struct {
    mode_tn mode;
    fmt_t fmt;
    const char *archive_file;
    const char *pass_dir;
    bool verbose;
    bool toc;
    bool make_dirs;
    bool preserve_mtime;
    bool force_overwrite;
    bool no_absolute_paths;
    bool safe_extract;
    bool no_overwrite;
    bool no_preserve_owner; /* root: leave extracted files root's */
    bool link_files;    /* -l: pass mode links rather than copies */
    char **patterns;    /* copy-in: members to select (all if none) */
    size_t npatterns;
    bool invert;        /* -f: select those that match none */
    bool null_names;    /* -0: the name list is NUL-separated */
    bool dereference;   /* -L: archive or copy what symlinks point to */
    bool reset_atime;   /* -a: put back the access times of files read */
    bool quiet;         /* --quiet: no "N blocks" */
    bool dot;           /* -V: a dot for each file */
    bool rename;        /* -r: ask for each member's name */
    bool swap_bytes;    /* -s (-b): swap the bytes of each halfword */
    bool swap_halfwords;    /* -S (-b): swap the halfwords of each word */
    bool numeric_ids;   /* -n: -tv shows uid and gid as numbers */
    uid_t owner_uid;    /* -R: the owner and/or group to give */
    gid_t owner_gid;
    bool owner_uid_set;
    bool owner_gid_set;
} options_t;

static int g_status = 0;

static void set_minor_error(void) { if (g_status < 1) g_status = 1; }
static void set_fatal_error(void) { g_status = 2; }

/* Diagnostics: "cpio: " and the message, or "cpio: WHAT: strerror(errno)". */
static void warnx_(const char *fmt, ...) {
    va_list ap;
    fputs("cpio: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void warn_errno(const char *what) {
    warnx_("%s: %s", what, strerror(errno));
}

static void usage(FILE *out) {
    fprintf(out,
        "usage: cpio -o [-v] [-H format] [-F archive] [-R owner] < name-list\n"
        "       cpio -i [-dfmntuv] [-E file] [-H format] [-F archive] [-R owner]\n"
        "               [--absolute-paths] [--insecure] [--no-overwrite] [pattern ...]\n"
        "       cpio -p [-dlmuv] [-R owner] directory < name-list\n");
}

/* A decimal id: all digits, in range. */
static bool parse_id(const char *s, unsigned long *out) {
    char *end;
    if (!*s || strspn(s, "0123456789") != strlen(s)) return false;
    errno = 0;
    *out = strtoul(s, &end, 10);
    return errno == 0 && *out <= 0xffffffffUL;
}

/* -R as GNU cpio takes it: USER, USER:GROUP, USER.GROUP, USER: (the user's
 * login group) or :GROUP; each a name or a number.  -1 with a message if
 * a name is unknown or the form is not one of these. */
static int parse_owner(const char *s, options_t *opt) {
    char *tmp = strdup(s);
    char *user, *group = NULL;
    bool login_group = false;
    int r = -1;
    if (!tmp) { warn_errno("-R"); return -1; }
    user = tmp;
    {
        char *sep = strchr(tmp, ':');
        if (!sep && !getpwnam(tmp)) sep = strchr(tmp, '.');
        if (sep) {
            *sep = '\0';
            group = sep + 1;
            if (*group == '\0') { group = NULL; login_group = true; }
        }
    }
    if (*user) {
        struct passwd *pw = getpwnam(user);
        unsigned long id;
        if (pw) {
            opt->owner_uid = pw->pw_uid;
            if (login_group) { opt->owner_gid = pw->pw_gid; opt->owner_gid_set = true; }
        } else if (parse_id(user, &id) && !login_group) {
            opt->owner_uid = (uid_t)id;
        } else {
            warnx_("invalid user: %s", user);
            goto out;
        }
        opt->owner_uid_set = true;
    } else if (!group) {
        warnx_("invalid owner: %s", s);
        goto out;
    }
    if (group) {
        struct group *gr = getgrnam(group);
        unsigned long id;
        if (gr) opt->owner_gid = gr->gr_gid;
        else if (parse_id(group, &id)) opt->owner_gid = (gid_t)id;
        else { warnx_("invalid group: %s", group); goto out; }
        opt->owner_gid_set = true;
    }
    r = 0;
out:
    free(tmp);
    return r;
}

/* The owner to give a file archived or found with UID and GID: -R's, or as
 * root (unless --no-preserve-owner) the archived one.  False if its owner
 * is to be left alone. */
static bool owner_for(const options_t *opt, uid_t uid, gid_t gid, uid_t *ou, gid_t *og) {
    if (opt->owner_uid_set || opt->owner_gid_set) {
        *ou = opt->owner_uid_set ? opt->owner_uid : (uid_t)-1;
        *og = opt->owner_gid_set ? opt->owner_gid : (gid_t)-1;
        return true;
    }
    if (geteuid() == 0 && !opt->no_preserve_owner) {
        *ou = uid;
        *og = gid;
        return true;
    }
    return false;
}

static int write_all(int fd, const void *buf, size_t n) {
    const uint8_t *p = (const uint8_t *)buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += (size_t)w;
        n -= (size_t)w;
    }
    return 0;
}

/* The I/O block: the archive is written in blocks of this size, the last
 * padded with zeros, and sizes are reported in them.  512 bytes, 5120
 * with -B, N with -C N, N * 512 with --block-size=N, as in GNU cpio. */
static size_t g_block_size = 512;

/* The archive being written, a block at a time. */
static struct {
    int fd;
    unsigned char *buf;
    size_t len;
    uint64_t total;     /* bytes written */
} g_out;

static int ar_write(const void *p, size_t n) {
    const unsigned char *s = p;
    while (n > 0) {
        size_t c = g_block_size - g_out.len;
        if (c > n) c = n;
        memcpy(g_out.buf + g_out.len, s, c);
        g_out.len += c;
        s += c;
        n -= c;
        if (g_out.len == g_block_size) {
            if (write_all(g_out.fd, g_out.buf, g_block_size) < 0) return -1;
            g_out.total += g_block_size;
            g_out.len = 0;
        }
    }
    return 0;
}

/* Pad the last block with zeros and write it. */
static int ar_finish(void) {
    if (g_out.len == 0) return 0;
    memset(g_out.buf + g_out.len, 0, g_block_size - g_out.len);
    if (write_all(g_out.fd, g_out.buf, g_block_size) < 0) return -1;
    g_out.total += g_block_size;
    g_out.len = 0;
    return 0;
}

static bool g_dots;     /* -V has written a dot */

/* A file has been archived, extracted or copied: -v names it, -V marks
 * it with a dot, on standard error. */
static void note_file(const options_t *opt, const char *name) {
    if (opt->verbose) {
        fprintf(stderr, "%s\n", name);
    } else if (opt->dot) {
        fputc('.', stderr);
        g_dots = true;
    }
}

/* "N blocks" on standard error, as GNU cpio reports how much it moved,
 * unless --quiet or the run failed. */
static void report_blocks(const options_t *opt, uint64_t bytes) {
    uint64_t n = (bytes + g_block_size - 1) / g_block_size;
    if (g_dots) fputc('\n', stderr);    /* end -V's line of dots */
    if (opt->quiet || g_status >= 2) return;
    fprintf(stderr, "%ju block%s\n", (uintmax_t)n, n == 1 ? "" : "s");
}

static int make_parent_dirs(const char *path) {
    char tmp[PATH_MAX];
    size_t i;
    if (strlen(path) >= sizeof(tmp)) return -1;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (i = 1; tmp[i]; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0777) < 0 && errno != EEXIST) return -1;
            tmp[i] = '/';
        }
    }
    return 0;
}

static bool path_is_safe(const char *path, const options_t *opt) {
    if (opt->no_absolute_paths && path[0] == '/') return false;
    if (opt->safe_extract) {
        const char *p = path;
        while (*p) {
            if ((p == path || p[-1] == '/') && p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0'))
                return false;
            p++;
        }
    }
    return true;
}

/* The old binary format's 16-bit words are in the writing machine's byte
 * order: written here in this one's, read in whichever order the magic
 * number shows.  Its 32-bit numbers are two words, the high one first. */
static void put16(uint8_t *b, uint16_t v) { memcpy(b, &v, sizeof(v)); }
static uint16_t get16(bool le, const uint8_t *b) {
    return le ? (uint16_t)(b[1] << 8 | b[0]) : (uint16_t)(b[0] << 8 | b[1]);
}
/* Whether B holds the bin magic, and in which order (*LE). */
static bool bin_magic(const uint8_t *b, bool *le) {
    if (get16(true, b) == 070707) { *le = true; return true; }
    if (get16(false, b) == 070707) { *le = false; return true; }
    return false;
}

static int cpio_write_header_newc(const char *name, const struct stat *st, uint32_t filesize, uint32_t nlink) {
    char hdr[110 + 1];
    uint32_t namesz = (uint32_t)strlen(name) + 1;
    snprintf(hdr, sizeof(hdr),
        "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
        (unsigned)st->st_ino,
        (unsigned)st->st_mode,
        (unsigned)st->st_uid,
        (unsigned)st->st_gid,
        (unsigned)nlink,
        (unsigned)st->st_mtime,
        (unsigned)filesize,
        (unsigned)major(st->st_dev),
        (unsigned)minor(st->st_dev),
        (unsigned)major(st->st_rdev),
        (unsigned)minor(st->st_rdev),
        namesz,
        0U);
    if (ar_write(hdr, 110) < 0) return -1;
    if (ar_write(name, namesz) < 0) return -1;
    while ((110 + namesz) % 4 != 0) {
        if (ar_write("\0", 1) < 0) return -1;
        namesz++;
    }
    return 0;
}

static int cpio_write_header_odc(const char *name, const struct stat *st, uint64_t filesize, uint32_t nlink) {
    char hdr[128];
    uint32_t namesz = (uint32_t)strlen(name) + 1;
    snprintf(hdr, sizeof(hdr),
        "070707%06lo%06lo%06lo%06lo%06lo%06lo%06lo%011lo%06lo%011llo",
        (unsigned long)(st->st_dev & 0777777),
        (unsigned long)(st->st_ino & 0777777),
        (unsigned long)(st->st_mode & 0777777),
        (unsigned long)(st->st_uid & 0777777),
        (unsigned long)(st->st_gid & 0777777),
        (unsigned long)nlink,
        (unsigned long)(st->st_rdev & 0777777),
        (unsigned long)st->st_mtime,
        (unsigned long)namesz,
        (unsigned long long)filesize);
    if (ar_write(hdr, 76) < 0) return -1;
    return ar_write(name, namesz);
}

static int cpio_write_header_bin(const char *name, const struct stat *st, uint32_t filesize, uint32_t nlink) {
    uint8_t hdr[26];
    uint16_t namesz = (uint16_t)(strlen(name) + 1);
    memset(hdr, 0, sizeof(hdr));
    put16(hdr + 0, 070707);
    put16(hdr + 2, (uint16_t)st->st_dev);
    put16(hdr + 4, (uint16_t)st->st_ino);
    put16(hdr + 6, (uint16_t)st->st_mode);
    put16(hdr + 8, (uint16_t)st->st_uid);
    put16(hdr + 10, (uint16_t)st->st_gid);
    put16(hdr + 12, (uint16_t)nlink);
    put16(hdr + 14, (uint16_t)st->st_rdev);
    put16(hdr + 16, (uint16_t)((uint32_t)st->st_mtime >> 16));
    put16(hdr + 18, (uint16_t)((uint32_t)st->st_mtime & 0xffff));
    put16(hdr + 20, namesz);
    put16(hdr + 22, (uint16_t)(filesize >> 16));
    put16(hdr + 24, (uint16_t)(filesize & 0xffff));
    if (ar_write(hdr, sizeof(hdr)) < 0) return -1;
    if (ar_write(name, namesz) < 0) return -1;
    if ((26 + namesz) & 1) {
        if (ar_write("\0", 1) < 0) return -1;
    }
    return 0;
}

/* Copy SIZE bytes of the file PATH (open on FFD) into the archive.  The
 * header already promises SIZE, so a file that comes up short -- it
 * shrank, or a read failed -- is padded with zeros to keep the archive
 * in step, and diagnosed.  -1 only if writing the archive fails. */
static int copy_out_file_data(int ffd, uint64_t size, const char *path) {
    char buf[IO_BUFSZ];
    uint64_t left = size;
    while (left > 0) {
        size_t chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
        ssize_t r = read(ffd, buf, chunk);
        if (r < 0) {
            if (errno == EINTR) continue;
            warn_errno(path);
        } else if (r == 0) {
            warnx_("%s: shrank by %ju bytes; padding with zeros", path, (uintmax_t)left);
        }
        if (r <= 0) {
            set_minor_error();
            memset(buf, 0, sizeof(buf));
            while (left > 0) {
                chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
                if (ar_write(buf, chunk) < 0) return -1;
                left -= chunk;
            }
            return 0;
        }
        if (ar_write(buf, (size_t)r) < 0) return -1;
        left -= (uint64_t)r;
    }
    return 0;
}

/* Both hard-link tables, copy-out's and copy-in's, hash (dev, ino). */
#define LINK_BUCKETS 4096
static size_t link_hash(dev_t dev, uint64_t ino) {
    return (size_t)((ino * 0x9e3779b97f4a7c15ULL) ^ (uint64_t)dev) % LINK_BUCKETS;
}

/* -a: put back the access time a file had before it was read (on FD). */
static void reset_atime(int fd, const struct stat *st, const char *path) {
    struct timespec ts[2];
    ts[0] = st->st_atim;
    ts[1].tv_sec = 0;
    ts[1].tv_nsec = UTIME_OMIT;
    if (futimens(fd, ts) < 0) {
        warn_errno(path);
        set_minor_error();
    }
}

/* The largest member each format's size field holds. */
static uint64_t fmt_max_size(fmt_t fmt) {
    return fmt == FMT_ODC ? 077777777777ULL : 0xffffffffULL;
}

static const char *fmt_name(fmt_t fmt) {
    return fmt == FMT_NEWC ? "newc" : fmt == FMT_ODC ? "odc" : "bin";
}

/* Write one member: PATH's header, as ST describes it, and for a regular
 * file its data if WITH_DATA (else a size of 0, the newc way of naming
 * one more link).  0, -1 if PATH could not be archived, -2 if writing the
 * archive failed. */
static int write_member(options_t *opt, const char *path, struct stat *stp, bool with_data) {
    struct stat st = *stp;
    char lnk[PATH_MAX];
    ssize_t lsz = 0;
    uint64_t data_size = 0;
    uint32_t nlink_hdr = 1;
    int ffd = -1;

    if (S_ISLNK(st.st_mode)) {
        lsz = readlink(path, lnk, sizeof(lnk) - 1);
        if (lsz < 0) { warn_errno(path); set_minor_error(); return -1; }
        lnk[lsz] = '\0';
        data_size = (uint64_t)lsz;
    } else if (S_ISREG(st.st_mode)) {
        nlink_hdr = st.st_nlink;
        data_size = with_data ? (uint64_t)st.st_size : 0;
        if (data_size > 0) {
            ffd = open(path, O_RDONLY);
            if (ffd < 0) { warn_errno(path); set_minor_error(); return -1; }
        }
    }

    if (opt->owner_uid_set) st.st_uid = opt->owner_uid;
    if (opt->owner_gid_set) st.st_gid = opt->owner_gid;

    if (opt->fmt == FMT_NEWC) {
        if (cpio_write_header_newc(path, &st, (uint32_t)data_size, nlink_hdr) < 0) goto ioerr;
    } else if (opt->fmt == FMT_ODC) {
        if (cpio_write_header_odc(path, &st, data_size, nlink_hdr) < 0) goto ioerr;
    } else {
        if (cpio_write_header_bin(path, &st, (uint32_t)data_size, nlink_hdr) < 0) goto ioerr;
    }

    if (S_ISLNK(st.st_mode)) {
        if (ar_write(lnk, (size_t)lsz) < 0) goto ioerr;
    } else if (S_ISREG(st.st_mode) && data_size > 0) {
        if (copy_out_file_data(ffd, data_size, path) < 0) goto ioerr;
    }

    if (opt->fmt == FMT_NEWC) {
        while (data_size % 4 != 0) {
            if (ar_write("\0", 1) < 0) goto ioerr;
            data_size++;
        }
    } else if (opt->fmt == FMT_BIN && (data_size & 1)) {
        if (ar_write("\0", 1) < 0) goto ioerr;
    }

    note_file(opt, path);
    if (ffd >= 0) {
        if (opt->reset_atime) reset_atime(ffd, &st, path);
        close(ffd);
    }
    return 0;

ioerr:
    /* the archive is now broken mid-member: nothing after it can help */
    warn_errno("write error");
    set_fatal_error();
    if (ffd >= 0) close(ffd);
    return -2;
}

/* Copy-out's hard links, for newc, which puts a file's data on the last
 * of its names: the names of a file with more than one link are held
 * back until all have been seen, then written, the last with the data.
 * (odc and bin carry the data on every name and hold nothing back.) */
typedef struct out_link {
    dev_t dev;
    ino_t ino;
    char **names;
    struct stat *st;
    size_t n;
    struct out_link *next;
} out_link_t;

static out_link_t *g_out_links[LINK_BUCKETS];

/* Write a held-back file: every name, the last with the data.  Its record
 * is freed.  0, or -2 if writing the archive failed. */
static int out_link_flush(options_t *opt, out_link_t *l) {
    int r = 0;
    for (size_t i = 0; i < l->n; i++) {
        if (r != -2) {
            int w = write_member(opt, l->names[i], &l->st[i], i + 1 == l->n);
            if (w == -2) r = -2;
        }
        free(l->names[i]);
    }
    free(l->names);
    free(l->st);
    free(l);
    return r;
}

/* At the end of the list: write the files whose names did not all come. */
static int out_links_finish(options_t *opt) {
    int r = 0;
    for (size_t h = 0; h < LINK_BUCKETS; h++) {
        while (g_out_links[h]) {
            out_link_t *l = g_out_links[h];
            g_out_links[h] = l->next;
            if (out_link_flush(opt, l) == -2) r = -2;
        }
    }
    return r;
}

static int write_entry(options_t *opt, const char *path) {
    struct stat st;
    out_link_t *l, **pp;
    size_t h;

    if ((opt->dereference ? stat(path, &st) : lstat(path, &st)) < 0) {
        warn_errno(path);
        set_minor_error();
        return -1;
    }
    if (S_ISREG(st.st_mode) && (uint64_t)st.st_size > fmt_max_size(opt->fmt)) {
        warnx_("%s: too large for the %s format", path, fmt_name(opt->fmt));
        set_minor_error();
        return -1;
    }
    if (!S_ISREG(st.st_mode) || st.st_nlink < 2 || opt->fmt != FMT_NEWC)
        return write_member(opt, path, &st, true);

    h = link_hash(st.st_dev, st.st_ino);
    for (pp = &g_out_links[h]; *pp; pp = &(*pp)->next)
        if ((*pp)->dev == st.st_dev && (*pp)->ino == st.st_ino) break;
    l = *pp;
    if (!l) {
        l = calloc(1, sizeof(*l));
        if (!l) { warn_errno(path); set_minor_error(); return -1; }
        l->dev = st.st_dev;
        l->ino = st.st_ino;
        *pp = l;
    }
    {
        char **names = realloc(l->names, (l->n + 1) * sizeof(*names));
        struct stat *sts;
        if (names) l->names = names;
        sts = names ? realloc(l->st, (l->n + 1) * sizeof(*sts)) : NULL;
        if (sts) l->st = sts;
        if (!names || !sts || !(l->names[l->n] = strdup(path))) {
            warn_errno(path);
            set_minor_error();
            return -1;
        }
        l->st[l->n++] = st;
    }
    if (l->n < (size_t)st.st_nlink) return 0;
    *pp = l->next;
    return out_link_flush(opt, l);
}

static int write_trailer(options_t *opt) {
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_mode = S_IFREG;
    if (opt->fmt == FMT_NEWC) {
        return cpio_write_header_newc("TRAILER!!!", &st, 0, 1);
    }
    if (opt->fmt == FMT_ODC) {
        return cpio_write_header_odc("TRAILER!!!", &st, 0, 1);
    }
    return cpio_write_header_bin("TRAILER!!!", &st, 0, 1);
}

typedef struct {
    char *name;
    dev_t dev;
    uint64_t ino;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    nlink_t nlink;
    time_t mtime;
    uint64_t filesize;
    dev_t rdev;
} entry_t;

/* The archive being read, through a buffer.  The format is found by
 * looking at the first bytes without consuming them, so the archive
 * need not be seekable: a pipe works as well as a file. */
static struct {
    int fd;
    unsigned char buf[IO_BUFSZ];
    size_t pos, len;
    bool eof;
    uint64_t consumed;  /* bytes taken from the archive */
} g_in;

/* Make at least WANT (<= IO_BUFSZ) bytes available, if the archive has
 * them; return how many are. */
static size_t in_fill(size_t want) {
    if (g_in.len - g_in.pos >= want) return g_in.len - g_in.pos;
    memmove(g_in.buf, g_in.buf + g_in.pos, g_in.len - g_in.pos);
    g_in.len -= g_in.pos;
    g_in.pos = 0;
    while (g_in.len < want && !g_in.eof) {
        ssize_t r = read(g_in.fd, g_in.buf + g_in.len, sizeof(g_in.buf) - g_in.len);
        if (r < 0) {
            if (errno == EINTR) continue;
            warn_errno("read error");
            g_in.eof = true;
            break;
        }
        if (r == 0) { g_in.eof = true; break; }
        g_in.len += (size_t)r;
    }
    return g_in.len - g_in.pos;
}

/* Read exactly N bytes of the archive into P; -1 if it ends first. */
static int in_read(void *p, size_t n) {
    unsigned char *d = p;
    while (n > 0) {
        size_t avail = in_fill(1);
        size_t c;
        if (avail == 0) return -1;
        c = avail < n ? avail : n;
        memcpy(d, g_in.buf + g_in.pos, c);
        g_in.pos += c;
        g_in.consumed += c;
        d += c;
        n -= c;
    }
    return 0;
}

/* Skip N bytes of the archive; -1 if it ends first. */
static int in_skip(uint64_t n) {
    while (n > 0) {
        size_t avail = in_fill(1);
        size_t c;
        if (avail == 0) return -1;
        c = (uint64_t)avail < n ? avail : (size_t)n;
        g_in.pos += c;
        g_in.consumed += c;
        n -= c;
    }
    return 0;
}

/* Header parsers return 0, HDR_EOF if the archive ends inside the header,
 * or HDR_BAD if the header is malformed (its bytes are then consumed). */
#define HDR_EOF (-1)
#define HDR_BAD (-2)

/* A field of exactly N digits in BASE (8 or 16); false if any is not one. */
static bool num_field(const char *p, int n, int base, uint64_t *out) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) {
        int d;
        char c = p[i];
        if (c >= '0' && c <= '7') d = c - '0';
        else if (base == 16 && c >= '8' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    *out = v;
    return true;
}

/* The member name: NAMESZ bytes, the last a NUL, then PAD bytes. */
static int read_name(entry_t *e, uint64_t namesz, uint64_t pad) {
    if (namesz < 2 || namesz > PATH_MAX) {
        warnx_("malformed header: name length %ju", (uintmax_t)namesz);
        return HDR_BAD;
    }
    e->name = malloc((size_t)namesz);
    if (!e->name) { warn_errno("out of memory"); return HDR_BAD; }
    if (in_read(e->name, (size_t)namesz) < 0 || in_skip(pad) < 0) {
        free(e->name);
        e->name = NULL;
        return HDR_EOF;
    }
    if (e->name[namesz - 1] != '\0' || memchr(e->name, '\0', (size_t)namesz - 1)) {
        warnx_("malformed header: name not NUL-terminated");
        free(e->name);
        e->name = NULL;
        return HDR_BAD;
    }
    return 0;
}

static int parse_newc_header(entry_t *e) {
    char hdr[110];
    uint64_t f[13];

    if (in_read(hdr, sizeof(hdr)) < 0) return HDR_EOF;
    for (int i = 0; i < 13; i++) {
        if (!num_field(hdr + 6 + 8 * i, 8, 16, &f[i])) {
            warnx_("malformed header: bad number %.8s", hdr + 6 + 8 * i);
            return HDR_BAD;
        }
    }
    e->ino = (uint64_t)f[0];
    e->mode = (mode_t)f[1];
    e->uid = (uid_t)f[2];
    e->gid = (gid_t)f[3];
    e->nlink = (nlink_t)f[4];
    e->mtime = (time_t)f[5];
    e->filesize = f[6];
    e->dev = makedev(f[7], f[8]);
    e->rdev = makedev(f[9], f[10]);
    return read_name(e, f[11], (4 - (110 + f[11]) % 4) % 4);
}

static int parse_odc_header(entry_t *e) {
    static const int width[10] = { 6, 6, 6, 6, 6, 6, 6, 11, 6, 11 };
    char hdr[76];
    uint64_t f[10];
    const char *p = hdr + 6;

    if (in_read(hdr, sizeof(hdr)) < 0) return HDR_EOF;
    for (int i = 0; i < 10; p += width[i], i++) {
        if (!num_field(p, width[i], 8, &f[i])) {
            warnx_("malformed header: bad number %.*s", width[i], p);
            return HDR_BAD;
        }
    }
    e->dev = (dev_t)f[0];
    e->ino = f[1];
    e->mode = (mode_t)f[2];
    e->uid = (uid_t)f[3];
    e->gid = (gid_t)f[4];
    e->nlink = (nlink_t)f[5];
    e->rdev = (dev_t)f[6];
    e->mtime = (time_t)f[7];
    e->filesize = f[9];
    return read_name(e, f[8], 0);
}

static int parse_bin_header(entry_t *e) {
    uint8_t hdr[26];
    uint16_t namesz;
    bool le;
    if (in_read(hdr, 26) < 0) return HDR_EOF;
    if (!bin_magic(hdr, &le)) return HDR_BAD;
    namesz = get16(le, hdr + 20);
    e->dev = get16(le, hdr + 2);
    e->ino = get16(le, hdr + 4);
    e->mode = get16(le, hdr + 6);
    e->uid = get16(le, hdr + 8);
    e->gid = get16(le, hdr + 10);
    e->nlink = get16(le, hdr + 12);
    e->rdev = get16(le, hdr + 14);
    e->mtime = (time_t)(((uint32_t)get16(le, hdr + 16) << 16) | get16(le, hdr + 18));
    e->filesize = ((uint32_t)get16(le, hdr + 22) << 16) | get16(le, hdr + 24);
    return read_name(e, namesz, (26 + namesz) & 1);
}

/* The padding after SIZE bytes of member data. */
static uint64_t data_pad(fmt_t fmt, uint64_t size) {
    if (fmt == FMT_NEWC) return (4 - size % 4) % 4;
    if (fmt == FMT_BIN) return size & 1;
    return 0;
}

static int skip_data_and_pad(fmt_t fmt, uint64_t size) {
    return in_skip(size + data_pad(fmt, size));
}

static int write_sparse_aware(int outfd, const char *buf, size_t n, off_t *offset) {
    size_t i = 0;
    while (i < n) {
        size_t zstart = i;
        while (zstart < n && buf[zstart] != '\0') zstart++;
        if (zstart > i) {
            if (write_all(outfd, buf + i, zstart - i) < 0) return -1;
            *offset += (off_t)(zstart - i);
        }
        i = zstart;
        while (i < n && buf[i] == '\0') i++;
        if (i > zstart) {
            if (lseek(outfd, (off_t)(i - zstart), SEEK_CUR) < 0) {
                static char zeros[4096];
                size_t rem = i - zstart;
                while (rem > 0) {
                    size_t c = rem > sizeof(zeros) ? sizeof(zeros) : rem;
                    if (write_all(outfd, zeros, c) < 0) return -1;
                    rem -= c;
                }
            }
            *offset += (off_t)(i - zstart);
        }
    }
    return 0;
}

/* ls -l's mode string: the type, then rwx three times with s/S/t/T. */
static void mode_string(mode_t m, char out[11]) {
    static const char rwx[] = "rwxrwxrwx";
    out[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISCHR(m) ? 'c' :
             S_ISBLK(m) ? 'b' : S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-';
    for (int i = 0; i < 9; i++)
        out[1 + i] = (m & (0400 >> i)) ? rwx[i] : '-';
    if (m & S_ISUID) out[3] = (m & S_IXUSR) ? 's' : 'S';
    if (m & S_ISGID) out[6] = (m & S_IXGRP) ? 's' : 'S';
    if (m & S_ISVTX) out[9] = (m & S_IXOTH) ? 't' : 'T';
    out[10] = '\0';
}

/* -t: the member's name, or with -v a line as GNU cpio writes it:
 * mode, links, owner, group, size (or major, minor), date, name, and
 * for a symlink " -> target".  The member's data is passed over. */
static int list_entry(const entry_t *e, const options_t *opt) {
    char mbuf[11], tbuf[32], owner[16], group[16];
    char target[PATH_MAX];
    size_t tlen = 0;
    time_t now = time(NULL);
    time_t when = e->mtime;
    struct tm *tm;

    if (!opt->verbose) {
        printf("%s\n", e->name);
        return skip_data_and_pad(opt->fmt, e->filesize);
    }

    if (S_ISLNK(e->mode)) {
        tlen = e->filesize < sizeof(target) ? (size_t)e->filesize : sizeof(target) - 1;
        if (in_read(target, tlen) < 0) return -1;
        target[tlen] = '\0';
        if (in_skip(e->filesize - tlen + data_pad(opt->fmt, e->filesize)) < 0) return -1;
    } else if (skip_data_and_pad(opt->fmt, e->filesize) < 0) {
        return -1;
    }

    mode_string(e->mode, mbuf);
    {
        struct passwd *pw = opt->numeric_ids ? NULL : getpwuid(e->uid);
        struct group *gr = opt->numeric_ids ? NULL : getgrgid(e->gid);
        if (pw) snprintf(owner, sizeof(owner), "%.8s", pw->pw_name);
        else snprintf(owner, sizeof(owner), "%u", (unsigned)e->uid);
        if (gr) snprintf(group, sizeof(group), "%.8s", gr->gr_name);
        else snprintf(group, sizeof(group), "%u", (unsigned)e->gid);
    }
    tm = localtime(&when);
    if (!tm) {
        snprintf(tbuf, sizeof(tbuf), "%jd", (intmax_t)when);
    } else if (when > now || now - when > (time_t)(365.2425 * 24 * 60 * 60 / 2)) {
        strftime(tbuf, sizeof(tbuf), "%b %e  %Y", tm);
    } else {
        strftime(tbuf, sizeof(tbuf), "%b %e %H:%M", tm);
    }

    printf("%s %3lu %-8s %-8s ", mbuf, (unsigned long)e->nlink, owner, group);
    if (S_ISCHR(e->mode) || S_ISBLK(e->mode))
        printf("%3u, %3u ", (unsigned)major(e->rdev), (unsigned)minor(e->rdev));
    else
        printf("%8ju ", (uintmax_t)e->filesize);
    printf("%s %s", tbuf, e->name);
    if (S_ISLNK(e->mode)) printf(" -> %s", target);
    putchar('\n');
    return 0;
}

/* A name as given to the user: the member name, not "./name". */
static const char *shown(const char *full) {
    return strncmp(full, "./", 2) == 0 ? full + 2 : full;
}

/* Copy-in's hard links: one record for each archived (dev, ino) of a
 * regular member with more than one link.  The first name extracted with
 * data becomes the file; every other name is linked to it.  newc puts the
 * data on the last name, so earlier names (size 0) wait in PENDING. */
typedef struct in_link {
    dev_t dev;
    uint64_t ino;
    char *path;             /* the file, once extracted */
    char **pending;         /* names waiting for it */
    size_t npending;
    entry_t meta;           /* attributes, if no name ever brings data */
    struct in_link *next;
} in_link_t;

static in_link_t *g_in_links[LINK_BUCKETS];

static in_link_t *in_link_get(const entry_t *e) {
    size_t h = link_hash(e->dev, e->ino);
    in_link_t *l;
    for (l = g_in_links[h]; l; l = l->next)
        if (l->dev == e->dev && l->ino == e->ino) return l;
    l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    l->dev = e->dev;
    l->ino = e->ino;
    l->meta = *e;
    l->meta.name = NULL;
    l->next = g_in_links[h];
    g_in_links[h] = l;
    return l;
}

/* Make NEWPATH another name for OLDPATH, replacing what is there. */
static void make_link(const char *oldpath, const char *newpath) {
    if (unlink(newpath) < 0 && errno != ENOENT && errno != ENOTDIR) {
        warn_errno(shown(newpath));
        set_minor_error();
        return;
    }
    if (link(oldpath, newpath) < 0) {
        warn_errno(shown(newpath));
        set_minor_error();
    }
}

/* The file has been extracted at PATH: link the waiting names to it. */
static void in_link_extracted(in_link_t *l, const char *path) {
    l->path = strdup(path);
    if (!l->path) { warn_errno(shown(path)); set_minor_error(); return; }
    for (size_t i = 0; i < l->npending; i++) {
        make_link(l->path, l->pending[i]);
        free(l->pending[i]);
    }
    free(l->pending);
    l->pending = NULL;
    l->npending = 0;
}

static void apply_attrs(const char *full, const entry_t *e, const options_t *opt);

/* At the end of the archive: names whose data never came (an empty file,
 * or links whose other names were not archived) become an empty file and
 * links to it, as GNU cpio makes them. */
static void in_links_finish(const options_t *opt) {
    for (size_t h = 0; h < LINK_BUCKETS; h++) {
        in_link_t *l = g_in_links[h];
        while (l) {
            in_link_t *next = l->next;
            if (l->npending > 0) {
                const char *first = l->pending[0];
                int fd;
                unlink(first);
                fd = open(first, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, l->meta.mode & 07777);
                if (fd < 0) {
                    warn_errno(shown(first));
                    set_minor_error();
                } else {
                    close(fd);
                    apply_attrs(first, &l->meta, opt);
                    for (size_t i = 1; i < l->npending; i++) make_link(first, l->pending[i]);
                }
                for (size_t i = 0; i < l->npending; i++) free(l->pending[i]);
                free(l->pending);
            }
            free(l->path);
            free(l);
            l = next;
        }
        g_in_links[h] = NULL;
    }
}

/* Owner, permissions and times for an extracted file.  The mode is set
 * outright, not through the umask, and after the owner, since chown()
 * clears the set-ID bits. */
static void apply_attrs(const char *full, const entry_t *e, const options_t *opt) {
    uid_t ou;
    gid_t og;
    if (owner_for(opt, e->uid, e->gid, &ou, &og) && lchown(full, ou, og) < 0) {
        warn_errno(shown(full));
        set_minor_error();
    }
    if (!S_ISLNK(e->mode) && chmod(full, e->mode & 07777) < 0) {
        warn_errno(shown(full));
        set_minor_error();
    }
    if (opt->preserve_mtime && !S_ISLNK(e->mode)) {
        struct timespec ts[2];
        ts[0].tv_sec = e->mtime; ts[0].tv_nsec = 0;
        ts[1].tv_sec = e->mtime; ts[1].tv_nsec = 0;
        if (utimensat(AT_FDCWD, full, ts, AT_SYMLINK_NOFOLLOW) < 0) set_minor_error();
    }
}

/* Symlinks from the archive that could lead out of the tree -- an
 * absolute target, or one with a ".." component -- are not made until
 * the archive has been read, so no later member can be written through
 * one (as GNU tar does).  Meanwhile an empty, mode-0 regular file holds
 * the name, which makes "link/anything" fail; at the end it is replaced
 * by the symlink if it is still that same file.  Symlinks already on
 * disk are not this program's to judge and are followed as usual. */
typedef struct deferred_link {
    char *path;
    char *target;
    dev_t dev;
    ino_t ino;
    entry_t meta;
    struct deferred_link *next;
} deferred_link_t;

static deferred_link_t *g_deferred;

static bool symlink_escapes(const char *target) {
    const char *p = target;
    if (target[0] == '/') return true;
    while (*p) {
        if ((p == target || p[-1] == '/') && p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0'))
            return true;
        p++;
    }
    return false;
}

static void defer_symlink(const char *full, const char *target, const entry_t *e) {
    deferred_link_t *d = calloc(1, sizeof(*d));
    struct stat st;
    int fd;
    if (!d || !(d->path = strdup(full)) || !(d->target = strdup(target))) {
        warn_errno(e->name);
        set_minor_error();
        if (d) { free(d->path); free(d); }
        return;
    }
    fd = open(full, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0);
    if (fd < 0 || fstat(fd, &st) < 0) {
        warn_errno(e->name);
        set_minor_error();
        if (fd >= 0) close(fd);
        free(d->path); free(d->target); free(d);
        return;
    }
    close(fd);
    d->dev = st.st_dev;
    d->ino = st.st_ino;
    d->meta = *e;
    d->meta.name = NULL;
    d->next = g_deferred;
    g_deferred = d;
}

static void apply_attrs(const char *full, const entry_t *e, const options_t *opt);

/* The archive is read: make the held-back symlinks. */
static void deferred_symlinks_finish(const options_t *opt) {
    while (g_deferred) {
        deferred_link_t *d = g_deferred;
        struct stat st;
        g_deferred = d->next;
        if (lstat(d->path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0 &&
            st.st_dev == d->dev && st.st_ino == d->ino) {
            if (unlink(d->path) < 0 || symlink(d->target, d->path) < 0) {
                warn_errno(shown(d->path));
                set_minor_error();
            } else {
                apply_attrs(d->path, &d->meta, opt);
            }
        }
        free(d->path);
        free(d->target);
        free(d);
    }
}

/* Directories are made writable by their owner and given their own mode,
 * and with -m their time, only when everything has been extracted: a
 * read-only directory would refuse its own contents, and every file
 * made in one changes its time.  The list runs newest first, so a
 * directory is finished before the one holding it. */
typedef struct dir_fix {
    char *path;
    entry_t meta;
    struct dir_fix *next;
} dir_fix_t;

static dir_fix_t *g_dir_fixes;

static void defer_dir(const char *path, const entry_t *e) {
    dir_fix_t *d = calloc(1, sizeof(*d));
    if (!d || !(d->path = strdup(path))) {
        free(d);
        warn_errno(shown(path));
        set_minor_error();
        return;
    }
    d->meta = *e;
    d->meta.name = NULL;
    d->next = g_dir_fixes;
    g_dir_fixes = d;
}

static void dir_fixes_finish(const options_t *opt) {
    while (g_dir_fixes) {
        dir_fix_t *d = g_dir_fixes;
        g_dir_fixes = d->next;
        apply_attrs(d->path, &d->meta, opt);
        free(d->path);
        free(d);
    }
}

/* Whether copy-in takes the member NAME: it matches a pattern (fnmatch,
 * no flags, as GNU cpio), or with -f matches none; every member if no
 * patterns were given. */
static bool selected(const char *name, const options_t *opt) {
    bool match = opt->npatterns == 0;
    for (size_t i = 0; !match && i < opt->npatterns; i++)
        match = fnmatch(opt->patterns[i], name, 0) == 0;
    return opt->npatterns == 0 ? true : match != opt->invert;
}

/* -r: ask on the terminal what to call member NAME.  The new name, in a
 * static buffer, or NULL to skip the member (an empty reply). */
static const char *ask_rename(const char *name) {
    static FILE *tty;
    static char reply[PATH_MAX];
    size_t len;
    if (!tty && !(tty = fopen("/dev/tty", "r+"))) {
        warn_errno("/dev/tty");
        set_minor_error();
        return NULL;
    }
    fprintf(tty, "rename %s -> ", name);
    fflush(tty);
    if (!fgets(reply, sizeof(reply), tty)) return NULL;
    len = strlen(reply);
    if (len && reply[len - 1] == '\n') reply[--len] = '\0';
    return len ? reply : NULL;
}

static int extract_entry(const entry_t *e, const options_t *opt, const char *base) {
    char full[PATH_MAX];
    int fd = -1;
    uint64_t left = e->filesize;
    char buf[IO_BUFSZ];
    bool made = true;   /* the member is on disk, to be given its attributes */
    entry_t renamed;    /* -r: the member under its new name */

    if (strcmp(e->name, "TRAILER!!!") == 0) return 1;
    if (!selected(e->name, opt)) return skip_data_and_pad(opt->fmt, e->filesize);
    if (opt->toc) return list_entry(e, opt);
    if (opt->rename) {
        const char *to = ask_rename(e->name);
        if (!to) return skip_data_and_pad(opt->fmt, e->filesize);
        renamed = *e;
        renamed.name = (char *)to;
        e = &renamed;
    }
    if (!path_is_safe(e->name, opt)) {
        warnx_("%s: unsafe name, not extracted", e->name);
        set_minor_error();
        return skip_data_and_pad(opt->fmt, e->filesize);
    }

    if (snprintf(full, sizeof(full), "%s/%s", base ? base : ".", e->name) >= (int)sizeof(full)) {
        warnx_("%s: name too long", e->name);
        set_minor_error();
        return skip_data_and_pad(opt->fmt, e->filesize);
    }

    if (opt->no_overwrite && access(full, F_OK) == 0) {
        warnx_("%s: exists, not overwritten", e->name);
        return skip_data_and_pad(opt->fmt, e->filesize);
    }

    /* Without -u, a file on disk no older than the member stays. */
    if (!opt->force_overwrite && !S_ISDIR(e->mode)) {
        struct stat cur;
        if (lstat(full, &cur) == 0 && !S_ISDIR(cur.st_mode) && cur.st_mtime >= e->mtime) {
            warnx_("%s not created: newer or same age version exists", e->name);
            return skip_data_and_pad(opt->fmt, e->filesize);
        }
    }

    if (opt->make_dirs && make_parent_dirs(full) < 0) {
        warn_errno(e->name);
        set_minor_error();
        return skip_data_and_pad(opt->fmt, e->filesize);
    }

    if (S_ISDIR(e->mode)) {
        made = false;   /* its attributes come at the end */
        if (mkdir(full, (e->mode & 07777) | S_IRWXU) < 0 && errno != EEXIST) {
            warn_errno(e->name); set_minor_error();
        } else {
            defer_dir(full, e);
        }
    } else if (S_ISLNK(e->mode)) {
        char *target;
        if (e->filesize >= PATH_MAX) {
            warnx_("%s: symbolic link target too long", e->name);
            set_minor_error();
            return skip_data_and_pad(opt->fmt, e->filesize);
        }
        target = malloc((size_t)e->filesize + 1);
        if (!target) {
            warn_errno(e->name);
            set_minor_error();
            return skip_data_and_pad(opt->fmt, e->filesize);
        }
        if (in_read(target, (size_t)e->filesize) < 0) { free(target); return -1; }
        target[e->filesize] = '\0';
        unlink(full);
        if (opt->safe_extract && symlink_escapes(target)) {
            defer_symlink(full, target, e);   /* its owner is set then */
            made = false;
        } else if (symlink(target, full) < 0) {
            warn_errno(e->name);
            set_minor_error();
            made = false;
        }
        free(target);
        left = 0;
    } else if (S_ISFIFO(e->mode)) {
        if (mkfifo(full, e->mode & 07777) < 0 && errno != EEXIST) {
            warn_errno(e->name); set_minor_error();
            made = false;
        }
    } else if (S_ISCHR(e->mode) || S_ISBLK(e->mode)) {
        made = false;
        if (geteuid() != 0) {
            warnx_("%s: device not created: only root may", e->name);
            set_minor_error();
        } else if (mknod(full, e->mode, e->rdev) < 0 && errno != EEXIST) {
            warn_errno(e->name); set_minor_error();
        } else {
            made = true;
        }
    } else if (S_ISSOCK(e->mode)) {
        warnx_("%s: socket not created", e->name);
        set_minor_error();
        made = false;
    } else {
        off_t outoff = 0;
        in_link_t *l = NULL;
        if (e->nlink > 1) {
            l = in_link_get(e);
            if (l && l->path) {
                /* another name for a file already extracted */
                make_link(l->path, full);
                note_file(opt, e->name);
                return skip_data_and_pad(opt->fmt, e->filesize);
            }
            if (l && e->filesize == 0 && opt->fmt == FMT_NEWC) {
                /* newc: the data comes with a later name */
                char **p = realloc(l->pending, (l->npending + 1) * sizeof(*p));
                if (p) {
                    l->pending = p;
                    p[l->npending] = strdup(full);
                    if (p[l->npending]) {
                        l->npending++;
                        note_file(opt, e->name);
                        return 0;
                    }
                }
                warn_errno(e->name);
                set_minor_error();
                return 0;
            }
        }
        /* Remove any pre-planted symlink at the destination so the write
         * is not redirected outside the extraction tree (archive symlink
         * traversal); O_NOFOLLOW guards hosts whose open(2) honours it. */
        unlink(full);
        fd = open(full, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, e->mode & 07777);
        if (fd < 0) {
            warn_errno(e->name);
            set_minor_error();
            return skip_data_and_pad(opt->fmt, e->filesize);
        }
        /* -s, -S: swap only what divides evenly, as GNU cpio does */
        bool swap_bytes = opt->swap_bytes, swap_halves = opt->swap_halfwords;
        if (swap_halves && e->filesize % 4 != 0) {
            warnx_("cannot swap halfwords of %s: odd number of halfwords", e->name);
            swap_halves = false;
        }
        if (swap_bytes && e->filesize % 2 != 0) {
            warnx_("cannot swap bytes of %s: odd number of bytes", e->name);
            swap_bytes = false;
        }
        while (left > 0) {
            size_t chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
            if (in_read(buf, chunk) < 0) { close(fd); return -1; }
            left -= chunk;
            /* a chunk is whole words: IO_BUFSZ, or the rest of the file */
            if (swap_halves) {
                for (size_t i = 0; i + 4 <= chunk; i += 4) {
                    char t0 = buf[i], t1 = buf[i + 1];
                    buf[i] = buf[i + 2]; buf[i + 1] = buf[i + 3];
                    buf[i + 2] = t0; buf[i + 3] = t1;
                }
            }
            if (swap_bytes) {
                for (size_t i = 0; i + 2 <= chunk; i += 2) {
                    char t = buf[i];
                    buf[i] = buf[i + 1];
                    buf[i + 1] = t;
                }
            }
            if (write_sparse_aware(fd, buf, chunk, &outoff) < 0) {
                warn_errno(e->name);
                set_minor_error();
                close(fd);
                return in_skip(left + data_pad(opt->fmt, e->filesize));
            }
        }
        if (ftruncate(fd, (off_t)e->filesize) < 0) {
            warn_errno(e->name);
            set_minor_error();
        }
        close(fd);
        if (l) in_link_extracted(l, full);
    }

    if (in_skip(left + data_pad(opt->fmt, e->filesize)) < 0) return -1;

    if (made) apply_attrs(full, e, opt);
    note_file(opt, e->name);
    return 0;
}

/* Whether a header of FMT starts at the read position. */
static bool magic_here(fmt_t fmt) {
    size_t n = fmt == FMT_BIN ? 2 : 6;
    const unsigned char *b;
    if (in_fill(n) < n) return false;
    b = g_in.buf + g_in.pos;
    if (fmt == FMT_BIN) { bool le; return bin_magic(b, &le); }
    if (fmt == FMT_ODC) return memcmp(b, "070707", 6) == 0;
    return memcmp(b, "070701", 6) == 0 || memcmp(b, "070702", 6) == 0;
}

/* The format of the archive, from its first bytes, which stay unread. */
static fmt_t detect_fmt(void) {
    size_t avail;
    in_fill(6);
    if (magic_here(FMT_NEWC)) return FMT_NEWC;
    if (magic_here(FMT_ODC)) return FMT_ODC;
    if (magic_here(FMT_BIN)) return FMT_BIN;
    /* Junk first: the first header in the buffer, wherever it is,
     * names the format; the junk is skipped and reported later. */
    avail = in_fill(sizeof(g_in.buf));
    for (size_t i = 0; i + 1 < avail; i++) {
        const unsigned char *b = g_in.buf + g_in.pos + i;
        if (i + 6 <= avail && memcmp(b, "0707", 4) == 0 && b[4] == '0') {
            if (b[5] == '1' || b[5] == '2') return FMT_NEWC;
            if (b[5] == '7') return FMT_ODC;
        }
        bool le;
        if (bin_magic(b, &le)) return FMT_BIN;
    }
    return FMT_NEWC;
}

/* The next name of copy-out's or pass mode's list on standard input: a
 * line, or with -0 a NUL-terminated string, of any length.  NULL at the
 * end.  Empty names are passed over; a name too long for a path is
 * reported and passed over, not cut in two. */
static char *next_name(const options_t *opt) {
    static char *buf;
    static size_t cap;
    ssize_t n;
    while ((n = getdelim(&buf, &cap, opt->null_names ? '\0' : '\n', stdin)) > 0) {
        if (buf[n - 1] == (opt->null_names ? '\0' : '\n')) buf[--n] = '\0';
        if (n == 0) continue;
        if (n >= PATH_MAX) {
            warnx_("%.40s...: name too long", buf);
            set_minor_error();
            continue;
        }
        return buf;
    }
    if (ferror(stdin)) {
        warn_errno("standard input");
        set_minor_error();
    }
    return NULL;
}

static int mode_out(options_t *opt) {
    int afd = (opt->archive_file && strcmp(opt->archive_file, "-") != 0)
              ? open(opt->archive_file, O_WRONLY | O_CREAT | O_TRUNC, 0666)
              : STDOUT_FILENO;
    const char *name;
    if (afd < 0) { warn_errno(opt->archive_file); return 2; }
    g_out.fd = afd;
    g_out.buf = malloc(g_block_size);
    if (!g_out.buf) { warn_errno("out of memory"); return 2; }
    while ((name = next_name(opt)) != NULL) {
        if (write_entry(opt, name) == -2) break;
    }
    if (g_status < 2) out_links_finish(opt);
    if (g_status < 2 && (write_trailer(opt) < 0 || ar_finish() < 0)) {
        warn_errno("write error");
        set_fatal_error();
    }
    if (afd != STDOUT_FILENO && close(afd) < 0 && g_status < 2) {
        warn_errno("write error");
        set_fatal_error();
    }
    free(g_out.buf);
    report_blocks(opt, g_out.total);
    return g_status;
}

static int mode_in(options_t *opt) {
    int afd = (opt->archive_file && strcmp(opt->archive_file, "-") != 0)
              ? open(opt->archive_file, O_RDONLY)
              : STDIN_FILENO;
    if (afd < 0) { warn_errno(opt->archive_file); return 2; }
    g_in.fd = afd;

    if (opt->fmt == (fmt_t)-1)
        opt->fmt = detect_fmt();

    /* Until the trailer: anything that is not a header is junk, skipped
     * to the next one and reported; the archive ending first is fatal. */
    bool any = false;
    for (;;) {
        entry_t e;
        int r;
        uint64_t junk = 0;
        while (!magic_here(opt->fmt) && in_fill(1) > 0) {
            g_in.pos++;
            g_in.consumed++;
            junk++;
        }
        /* junk with no header after it is only the archive ending early */
        if (junk > 0 && in_fill(1) > 0)
            warnx_("warning: skipped %ju bytes of junk", (uintmax_t)junk);
        if (in_fill(1) == 0) {
            warnx_(any ? "premature end of file" : "premature end of archive");
            set_fatal_error();
            break;
        }
        memset(&e, 0, sizeof(e));
        if (opt->fmt == FMT_NEWC) r = parse_newc_header(&e);
        else if (opt->fmt == FMT_ODC) r = parse_odc_header(&e);
        else r = parse_bin_header(&e);
        if (r == HDR_BAD) { set_minor_error(); continue; }
        if (r == HDR_EOF) {
            warnx_("premature end of file");
            set_fatal_error();
            break;
        }
        any = true;
        r = extract_entry(&e, opt, ".");
        free(e.name);
        if (r == 1) break;
        if (r < 0) {
            warnx_("premature end of file");
            set_fatal_error();
            break;
        }
    }

    in_links_finish(opt);
    deferred_symlinks_finish(opt);
    dir_fixes_finish(opt);
    if (afd != STDIN_FILENO) close(afd);
    report_blocks(opt, g_in.consumed);
    return g_status;
}

/* Pass mode's hard links: the copy made of each (dev, ino) with more than
 * one link, so that its other names are linked to the copy. */
typedef struct pass_link {
    dev_t dev;
    ino_t ino;
    char *target;
    struct pass_link *next;
} pass_link_t;

static pass_link_t *g_pass_links[LINK_BUCKETS];
static uint64_t g_pass_bytes;   /* data copied, for the block count */

static const char *pass_link_find(const struct stat *st) {
    pass_link_t *l;
    for (l = g_pass_links[link_hash(st->st_dev, st->st_ino)]; l; l = l->next)
        if (l->dev == st->st_dev && l->ino == st->st_ino) return l->target;
    return NULL;
}

static void pass_link_add(const struct stat *st, const char *target) {
    size_t h = link_hash(st->st_dev, st->st_ino);
    pass_link_t *l = calloc(1, sizeof(*l));
    if (!l || !(l->target = strdup(target))) { free(l); return; }   /* copies, then */
    l->dev = st->st_dev;
    l->ino = st->st_ino;
    l->next = g_pass_links[h];
    g_pass_links[h] = l;
}

static void pass_links_free(void) {
    for (size_t h = 0; h < LINK_BUCKETS; h++) {
        while (g_pass_links[h]) {
            pass_link_t *l = g_pass_links[h];
            g_pass_links[h] = l->next;
            free(l->target);
            free(l);
        }
    }
}

/* Clear the way for a copy of SRC (described by ST) at TARGET.  What is
 * there is removed rather than written into -- a symlink there would take
 * the data elsewhere -- unless it is a directory, the source itself, or
 * (without -u) no older than the source.  0 to go on, 1 if the file there
 * stays, -1 on an error. */
static int prepare_dest(const char *src, const char *target, const struct stat *st,
                        const options_t *opt) {
    struct stat dst_st;
    if (lstat(target, &dst_st) < 0) return 0;
    if (S_ISDIR(dst_st.st_mode)) {
        errno = EISDIR;
        warn_errno(target);
        set_minor_error();
        return -1;
    }
    if (!opt->force_overwrite && dst_st.st_mtime >= st->st_mtime) {
        warnx_("%s not created: newer or same age version exists", target);
        return 1;
    }
    if (dst_st.st_dev == st->st_dev && dst_st.st_ino == st->st_ino) {
        warnx_("%s: is the same file as its copy", src);
        set_minor_error();
        return -1;
    }
    if (unlink(target) < 0) {
        warn_errno(target);
        set_minor_error();
        return -1;
    }
    return 0;
}

/* Owner, then mode (chown() clears the set-ID bits), then with -m times,
 * for a copy just made at TARGET (open on FD, or -1). */
static int finish_copy(const char *target, const struct stat *st, const options_t *opt, int fd) {
    uid_t ou;
    gid_t og;
    int r = 0;
    if (owner_for(opt, st->st_uid, st->st_gid, &ou, &og) &&
        (fd >= 0 ? fchown(fd, ou, og) : lchown(target, ou, og)) < 0) {
        warn_errno(target);
        set_minor_error();
        r = -1;
    }
    if (!S_ISLNK(st->st_mode) &&
        (fd >= 0 ? fchmod(fd, st->st_mode & 07777) : chmod(target, st->st_mode & 07777)) < 0) {
        warn_errno(target);
        set_minor_error();
        r = -1;
    }
    if (opt->preserve_mtime && !S_ISLNK(st->st_mode)) {
        struct timespec ts[2];
        ts[0].tv_sec = st->st_atime; ts[0].tv_nsec = 0;
        ts[1].tv_sec = st->st_mtime; ts[1].tv_nsec = 0;
        if ((fd >= 0 ? futimens(fd, ts) : utimensat(AT_FDCWD, target, ts, 0)) < 0) {
            warn_errno(target);
            set_minor_error();
            r = -1;
        }
    }
    return r;
}

/* Copy SRC to DST/SRC.  0 if copied, 1 if a newer file there was kept,
 * -1 on an error (already reported). */
static int copy_one_path(const char *src, const char *dst, const options_t *opt) {
    struct stat st;
    char buf[IO_BUFSZ];
    char target[PATH_MAX];
    int in, out;
    bool ok = true;
    if ((opt->dereference ? stat(src, &st) : lstat(src, &st)) < 0) {
        warn_errno(src);
        set_minor_error();
        return -1;
    }
    if (snprintf(target, sizeof(target), "%s/%s", dst, src) >= (int)sizeof(target)) {
        warnx_("%s: name too long", src);
        set_minor_error();
        return -1;
    }
    if (opt->make_dirs && make_parent_dirs(target) < 0) { warn_errno(target); set_minor_error(); return -1; }

    if (S_ISDIR(st.st_mode)) {
        if (mkdir(target, (st.st_mode & 07777) | S_IRWXU) < 0 && errno != EEXIST) {
            warn_errno(target);
            set_minor_error();
        } else {
            entry_t meta;
            memset(&meta, 0, sizeof(meta));
            meta.mode = st.st_mode;
            meta.uid = st.st_uid;
            meta.gid = st.st_gid;
            meta.mtime = st.st_mtime;
            defer_dir(target, &meta);
        }
        return 0;
    }
    if (S_ISLNK(st.st_mode)) {
        ssize_t n = readlink(src, buf, sizeof(buf) - 1);
        if (n < 0) { warn_errno(src); set_minor_error(); return -1; }
        buf[n] = '\0';
        switch (prepare_dest(src, target, &st, opt)) {
        case 1: return 1;
        case -1: return -1;
        }
        if (symlink(buf, target) < 0) {
            warn_errno(target);
            set_minor_error();
            return -1;
        }
        return finish_copy(target, &st, opt, -1);
    }
    if (S_ISSOCK(st.st_mode)) {
        warnx_("%s: socket not copied", src);
        set_minor_error();
        return -1;
    }
    if ((S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) && geteuid() != 0) {
        warnx_("%s: device not created: only root may", src);
        set_minor_error();
        return -1;
    }

    switch (prepare_dest(src, target, &st, opt)) {
    case 1: return 1;
    case -1: return -1;
    }

    if (S_ISFIFO(st.st_mode) || S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        int r = S_ISFIFO(st.st_mode) ? mkfifo(target, st.st_mode & 07777)
                                     : mknod(target, st.st_mode, st.st_rdev);
        if (r < 0) { warn_errno(target); set_minor_error(); return -1; }
        return finish_copy(target, &st, opt, -1);
    }

    /* A regular file.  Another name for one copied already is linked to
     * that copy; with -l, the copy is a link to the source itself. */
    if (st.st_nlink > 1) {
        const char *first = pass_link_find(&st);
        if (first) {
            if (link(first, target) < 0) { warn_errno(target); set_minor_error(); return -1; }
            return 0;
        }
    }
    if (opt->link_files && link(src, target) == 0) {
        if (st.st_nlink > 1) pass_link_add(&st, target);
        return 0;
    }

    in = open(src, O_RDONLY);
    if (in < 0) { warn_errno(src); set_minor_error(); return -1; }
    out = open(target, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, st.st_mode & 07777);
    if (out < 0) { warn_errno(target); set_minor_error(); close(in); return -1; }
    for (;;) {
        ssize_t r = read(in, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) continue;
            warn_errno(src);
            set_minor_error();
            ok = false;
            break;
        }
        if (r == 0) break;
        g_pass_bytes += (uint64_t)r;
        if (write_all(out, buf, (size_t)r) < 0) {
            warn_errno(target);
            set_minor_error();
            ok = false;
            break;
        }
    }
    if (opt->reset_atime) reset_atime(in, &st, src);
    close(in);
    if (st.st_nlink > 1) pass_link_add(&st, target);
    finish_copy(target, &st, opt, out);
    close(out);
    return ok ? 0 : -1;
}

static int mode_pass(options_t *opt) {
    const char *name;
    while ((name = next_name(opt)) != NULL) {
        if (copy_one_path(name, opt->pass_dir, opt) == 0)
            note_file(opt, name);
    }
    dir_fixes_finish(opt);
    pass_links_free();
    report_blocks(opt, g_pass_bytes);
    return g_status;
}

/* Copy-in's patterns: the operands, and with -E a file's lines. */
static int add_pattern(options_t *opt, const char *p) {
    char **v = realloc(opt->patterns, (opt->npatterns + 1) * sizeof(*v));
    if (!v || !(v[opt->npatterns] = strdup(p))) {
        if (v) opt->patterns = v;
        warn_errno("pattern");
        return -1;
    }
    opt->patterns = v;
    opt->npatterns++;
    return 0;
}

static int read_patterns(const char *file, options_t *opt) {
    FILE *f = fopen(file, "r");
    char line[PATH_MAX];
    if (!f) { warn_errno(file); return -1; }
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') line[--len] = '\0';
        if (len == 0) continue;
        if (add_pattern(opt, line) < 0) { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

/* Long options: GNU cpio's names for the options above, and the safety
 * options, which are this cpio's own. */
enum {
    OPT_SAFE = 256, OPT_NO_ABS, OPT_ABS, OPT_INSECURE, OPT_NO_OVERWRITE,
    OPT_NUMERIC_OWNER, OPT_NO_PRESERVE_OWNER, OPT_QUIET, OPT_BLOCK_SIZE,
};

static const struct option long_opts[] = {
    { "create", no_argument, NULL, 'o' },
    { "extract", no_argument, NULL, 'i' },
    { "pass-through", no_argument, NULL, 'p' },
    { "list", no_argument, NULL, 't' },
    { "verbose", no_argument, NULL, 'v' },
    { "file", required_argument, NULL, 'F' },
    { "format", required_argument, NULL, 'H' },
    { "make-directories", no_argument, NULL, 'd' },
    { "preserve-modification-time", no_argument, NULL, 'm' },
    { "unconditional", no_argument, NULL, 'u' },
    { "link", no_argument, NULL, 'l' },
    { "nonmatching", no_argument, NULL, 'f' },
    { "pattern-file", required_argument, NULL, 'E' },
    { "owner", required_argument, NULL, 'R' },
    { "numeric-uid-gid", no_argument, NULL, 'n' },
    { "directory", required_argument, NULL, 'D' },
    { "null", no_argument, NULL, '0' },
    { "dereference", no_argument, NULL, 'L' },
    { "reset-access-time", no_argument, NULL, 'a' },
    { "io-size", required_argument, NULL, 'C' },
    { "block-size", required_argument, NULL, OPT_BLOCK_SIZE },
    { "quiet", no_argument, NULL, OPT_QUIET },
    { "dot", no_argument, NULL, 'V' },
    { "rename", no_argument, NULL, 'r' },
    { "swap", no_argument, NULL, 'b' },
    { "swap-bytes", no_argument, NULL, 's' },
    { "swap-halfwords", no_argument, NULL, 'S' },
    { "safe-extract", no_argument, NULL, OPT_SAFE },
    { "no-absolute-paths", no_argument, NULL, OPT_NO_ABS },
    { "no-absolute-filenames", no_argument, NULL, OPT_NO_ABS },
    { "absolute-paths", no_argument, NULL, OPT_ABS },
    { "absolute-filenames", no_argument, NULL, OPT_ABS },
    { "insecure", no_argument, NULL, OPT_INSECURE },
    { "no-overwrite", no_argument, NULL, OPT_NO_OVERWRITE },
    { "numeric-owner", no_argument, NULL, OPT_NUMERIC_OWNER },
    { "no-preserve-owner", no_argument, NULL, OPT_NO_PRESERVE_OWNER },
    { NULL, 0, NULL, 0 },
};

static bool set_mode(options_t *opt, mode_tn m) {
    if (opt->mode != MODE_NONE && opt->mode != m) {
        warnx_("only one of -o, -i and -p may be given");
        return false;
    }
    opt->mode = m;
    return true;
}

static bool set_format(options_t *opt, const char *f) {
    if (strcmp(f, "newc") == 0) opt->fmt = FMT_NEWC;
    else if (strcmp(f, "odc") == 0) opt->fmt = FMT_ODC;
    else if (strcmp(f, "bin") == 0) opt->fmt = FMT_BIN;
    else {
        warnx_("unknown archive format: %s", f);
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    options_t opt;
    int c;
    const char *in_file = NULL, *out_file = NULL, *dir = NULL;

    setlocale(LC_ALL, "");
    memset(&opt, 0, sizeof(opt));
    opt.no_absolute_paths = true;
    opt.safe_extract = true;
    opt.fmt = (fmt_t)-1;

    opterr = 0;
    while ((c = getopt_long(argc, argv, ":oipF:tvnH:C:R:dmulfE:cI:O:D:0LaBVrbsS", long_opts, NULL)) != -1) {
        switch (c) {
        case 'o': if (!set_mode(&opt, MODE_OUT)) return 2; break;
        case 'i': if (!set_mode(&opt, MODE_IN)) return 2; break;
        case 'p': if (!set_mode(&opt, MODE_PASS)) return 2; break;
        case 'F': opt.archive_file = optarg; break;
        case 't': opt.toc = true; break;
        case 'v': opt.verbose = true; break;
        case 'n': opt.numeric_ids = true; break;
        case 'H': if (!set_format(&opt, optarg)) return 2; break;
        case 'c': opt.fmt = FMT_NEWC; break;    /* GNU: the same as -H newc */
        case 'I': in_file = optarg; break;
        case 'O': out_file = optarg; break;
        case 'D': dir = optarg; break;
        case '0': opt.null_names = true; break;
        case 'L': opt.dereference = true; break;
        case 'a': opt.reset_atime = true; break;
        case 'R': if (parse_owner(optarg, &opt) < 0) return 2; break;
        case 'd': opt.make_dirs = true; break;
        case 'm': opt.preserve_mtime = true; break;
        case 'l': opt.link_files = true; break;
        case 'f': opt.invert = true; break;
        case 'E': if (read_patterns(optarg, &opt) < 0) return 2; break;
        case 'u': opt.force_overwrite = true; break;
        case 'B': g_block_size = 5120; break;
        case 'C':
        case OPT_BLOCK_SIZE: {
            unsigned long n;
            if (!parse_id(optarg, &n) || n == 0 || n > (c == 'C' ? 1UL << 24 : (1UL << 24) / 512)) {
                warnx_("invalid block size: %s", optarg);
                return 2;
            }
            g_block_size = c == 'C' ? (size_t)n : (size_t)n * 512;
            break;
        }
        case OPT_QUIET: opt.quiet = true; break;
        case 'V': opt.dot = true; break;
        case 'r': opt.rename = true; break;
        case 'b': opt.swap_bytes = opt.swap_halfwords = true; break;
        case 's': opt.swap_bytes = true; break;
        case 'S': opt.swap_halfwords = true; break;
        case OPT_SAFE: opt.safe_extract = true; break;
        case OPT_NO_ABS: opt.no_absolute_paths = true; break;
        case OPT_ABS: opt.no_absolute_paths = false; break;
        case OPT_INSECURE: opt.safe_extract = false; opt.no_absolute_paths = false; break;
        case OPT_NO_OVERWRITE: opt.no_overwrite = true; break;
        /* the owner is restored by number, the only way cpio archives
         * record it: as root that is the default, so this is it */
        case OPT_NUMERIC_OWNER: opt.no_preserve_owner = false; break;
        case OPT_NO_PRESERVE_OWNER: opt.no_preserve_owner = true; break;
        case ':':
            if (optopt > 0 && optopt < 256) warnx_("option -%c needs an argument", optopt);
            else warnx_("option %s needs an argument", argv[optind - 1]);
            usage(stderr);
            return 2;
        default:
            if (optopt > 0 && optopt < 256) warnx_("unknown option -%c", optopt);
            else warnx_("unknown option %s", argv[optind - 1]);
            usage(stderr);
            return 2;
        }
    }

    /* -t alone lists, as GNU cpio's does */
    if (opt.toc && opt.mode == MODE_NONE) opt.mode = MODE_IN;
    if (opt.mode == MODE_NONE) {
        warnx_("one of -o, -i and -p is needed");
        usage(stderr);
        return 2;
    }
    if (opt.toc && opt.mode != MODE_IN) {
        warnx_("-t lists an archive: it goes with -i");
        return 2;
    }
    if ((opt.swap_bytes || opt.swap_halfwords) && opt.mode != MODE_IN) {
        warnx_("-b, -s and -S swap extracted data: they go with -i");
        return 2;
    }
    if (opt.rename && opt.mode != MODE_IN) {
        warnx_("-r renames members as they are extracted: it goes with -i");
        return 2;
    }
    /* -I is copy-in's archive and -O copy-out's, each another -F */
    if (in_file) {
        if (opt.mode != MODE_IN) { warnx_("-I names the archive to read: it goes with -i"); return 2; }
        opt.archive_file = in_file;
    }
    if (out_file) {
        if (opt.mode != MODE_OUT) { warnx_("-O names the archive to write: it goes with -o"); return 2; }
        opt.archive_file = out_file;
    }
    if (dir && chdir(dir) < 0) {
        warn_errno(dir);
        return 2;
    }

    switch (opt.mode) {
    case MODE_OUT:
        if (optind < argc) {
            warnx_("too many arguments: %s (copy-out reads its names from standard input)", argv[optind]);
            return 2;
        }
        if (opt.fmt == (fmt_t)-1) opt.fmt = FMT_NEWC;
        return mode_out(&opt);
    case MODE_IN:
        for (int i = optind; i < argc; i++)
            if (add_pattern(&opt, argv[i]) < 0) return 2;
        return mode_in(&opt);
    default:
        if (optind >= argc) {
            warnx_("-p needs a destination directory");
            return 2;
        }
        if (optind + 1 < argc) {
            warnx_("too many arguments: %s", argv[optind + 1]);
            return 2;
        }
        if (opt.archive_file) {
            warnx_("-F names an archive: -p has none");
            return 2;
        }
        opt.pass_dir = argv[optind];
        return mode_pass(&opt);
    }
}
