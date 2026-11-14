#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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
    bool numeric_owner;
    bool numeric_ids;   /* -n: -tv shows uid and gid as numbers */
    uid_t owner_uid;
    gid_t owner_gid;
    bool owner_set;
} options_t;

typedef struct hardlink_ent {
    dev_t dev;
    ino_t ino;
    char *first_path;
    struct hardlink_ent *next;
} hardlink_ent_t;

static int g_status = 0;
static hardlink_ent_t *g_links = NULL;

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
        "usage: cpio -o [-v] [-H format] [-F archive] [-R user:group]\n"
        "       cpio -i [-t] [-v] [-dmu] [-H format] [-F archive] [--safe-extract] [--absolute-paths] [--insecure]\n"
        "       cpio -p [-v] [-dmu] directory\n");
}

static int parse_owner(const char *s, uid_t *uid, gid_t *gid) {
    char *tmp = strdup(s);
    char *sep;
    if (!tmp) return -1;
    sep = strchr(tmp, ':');
    if (!sep) { free(tmp); return -1; }
    *sep = '\0';
    *uid = (uid_t)strtol(tmp, NULL, 10);
    *gid = (gid_t)strtol(sep + 1, NULL, 10);
    free(tmp);
    return 0;
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

static int cpio_write_header_newc(int fd, const char *name, const struct stat *st, uint32_t filesize, uint32_t nlink) {
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
    if (write_all(fd, hdr, 110) < 0) return -1;
    if (write_all(fd, name, namesz) < 0) return -1;
    while ((110 + namesz) % 4 != 0) {
        if (write_all(fd, "\0", 1) < 0) return -1;
        namesz++;
    }
    return 0;
}

static int cpio_write_header_odc(int fd, const char *name, const struct stat *st, uint64_t filesize, uint32_t nlink) {
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
    if (write_all(fd, hdr, 76) < 0) return -1;
    return write_all(fd, name, namesz);
}

static int cpio_write_header_bin(int fd, const char *name, const struct stat *st, uint32_t filesize, uint32_t nlink) {
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
    if (write_all(fd, hdr, sizeof(hdr)) < 0) return -1;
    if (write_all(fd, name, namesz) < 0) return -1;
    if ((26 + namesz) & 1) {
        if (write_all(fd, "\0", 1) < 0) return -1;
    }
    return 0;
}

static int copy_out_file_data(int afd, int ffd, off_t size) {
    char buf[IO_BUFSZ];
    off_t left = size;
    while (left > 0) {
        size_t chunk = (left > (off_t)sizeof(buf)) ? sizeof(buf) : (size_t)left;
        ssize_t r = read(ffd, buf, chunk);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        if (write_all(afd, buf, (size_t)r) < 0) return -1;
        left -= r;
    }
    return 0;
}

static hardlink_ent_t *hl_find(dev_t dev, ino_t ino) {
    hardlink_ent_t *e;
    for (e = g_links; e; e = e->next)
        if (e->dev == dev && e->ino == ino) return e;
    return NULL;
}

static int hl_add(dev_t dev, ino_t ino, const char *path) {
    hardlink_ent_t *e = calloc(1, sizeof(*e));
    if (!e) return -1;
    e->dev = dev;
    e->ino = ino;
    e->first_path = strdup(path);
    if (!e->first_path) { free(e); return -1; }
    e->next = g_links;
    g_links = e;
    return 0;
}

static void hl_free(void) {
    while (g_links) {
        hardlink_ent_t *next = g_links->next;
        free(g_links->first_path);
        free(g_links);
        g_links = next;
    }
}

/* The largest member each format's size field holds. */
static uint64_t fmt_max_size(fmt_t fmt) {
    return fmt == FMT_ODC ? 077777777777ULL : 0xffffffffULL;
}

static const char *fmt_name(fmt_t fmt) {
    return fmt == FMT_NEWC ? "newc" : fmt == FMT_ODC ? "odc" : "bin";
}

static int write_entry(options_t *opt, int afd, const char *path) {
    struct stat st;
    char lnk[PATH_MAX];
    ssize_t lsz = 0;
    uint64_t data_size = 0;
    uint32_t nlink_hdr = 1;
    int ffd = -1;

    if (lstat(path, &st) < 0) {
        warn_errno(path);
        set_minor_error();
        return -1;
    }

    if (S_ISLNK(st.st_mode)) {
        lsz = readlink(path, lnk, sizeof(lnk) - 1);
        if (lsz < 0) { warn_errno(path); set_minor_error(); return -1; }
        lnk[lsz] = '\0';
        data_size = (uint32_t)lsz;
    } else if (S_ISREG(st.st_mode)) {
        hardlink_ent_t *h = NULL;
        if ((uint64_t)st.st_size > fmt_max_size(opt->fmt)) {
            warnx_("%s: too large for the %s format", path, fmt_name(opt->fmt));
            set_minor_error();
            return -1;
        }
        data_size = (uint64_t)st.st_size;
        nlink_hdr = st.st_nlink;
        if (st.st_nlink > 1) {
            h = hl_find(st.st_dev, st.st_ino);
            if (h) data_size = 0;
            else if (hl_add(st.st_dev, st.st_ino, path) < 0) {
                warn_errno(path);
                set_minor_error();
            }
        }
        if (data_size > 0) {
            ffd = open(path, O_RDONLY);
            if (ffd < 0) { warn_errno(path); set_minor_error(); return -1; }
        }
    }

    if (opt->owner_set) {
        st.st_uid = opt->owner_uid;
        st.st_gid = opt->owner_gid;
    }

    if (opt->fmt == FMT_NEWC) {
        if (cpio_write_header_newc(afd, path, &st, (uint32_t)data_size, nlink_hdr) < 0) goto ioerr;
    } else if (opt->fmt == FMT_ODC) {
        if (cpio_write_header_odc(afd, path, &st, data_size, nlink_hdr) < 0) goto ioerr;
    } else {
        if (cpio_write_header_bin(afd, path, &st, (uint32_t)data_size, nlink_hdr) < 0) goto ioerr;
    }

    if (S_ISLNK(st.st_mode)) {
        if (write_all(afd, lnk, (size_t)lsz) < 0) goto ioerr;
    } else if (S_ISREG(st.st_mode) && data_size > 0) {
        if (copy_out_file_data(afd, ffd, (off_t)data_size) < 0) goto ioerr;
    }

    if (opt->fmt == FMT_NEWC) {
        while (data_size % 4 != 0) {
            if (write_all(afd, "\0", 1) < 0) goto ioerr;
            data_size++;
        }
    } else if (opt->fmt == FMT_BIN && (data_size & 1)) {
        if (write_all(afd, "\0", 1) < 0) goto ioerr;
    }

    if (opt->verbose) fprintf(stderr, "%s\n", path);
    if (ffd >= 0) close(ffd);
    return 0;

ioerr:
    warn_errno("write error");
    set_minor_error();
    if (ffd >= 0) close(ffd);
    return -1;
}

static int write_trailer(options_t *opt, int afd) {
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_mode = S_IFREG;
    if (opt->fmt == FMT_NEWC) {
        return cpio_write_header_newc(afd, "TRAILER!!!", &st, 0, 1);
    }
    if (opt->fmt == FMT_ODC) {
        return cpio_write_header_odc(afd, "TRAILER!!!", &st, 0, 1);
    }
    return cpio_write_header_bin(afd, "TRAILER!!!", &st, 0, 1);
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

static int extract_entry(const entry_t *e, const options_t *opt, const char *base) {
    char full[PATH_MAX];
    int fd = -1;
    uint64_t left = e->filesize;
    char buf[IO_BUFSZ];

    if (strcmp(e->name, "TRAILER!!!") == 0) return 1;
    if (opt->toc) return list_entry(e, opt);
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

    if (opt->make_dirs && make_parent_dirs(full) < 0) {
        warn_errno(e->name);
        set_minor_error();
        return skip_data_and_pad(opt->fmt, e->filesize);
    }

    if (S_ISDIR(e->mode)) {
        if (mkdir(full, e->mode & 07777) < 0 && errno != EEXIST) {
            warn_errno(full); set_minor_error();
        }
    } else if (S_ISLNK(e->mode)) {
        char *target = malloc((size_t)e->filesize + 1);
        if (!target) {
            warn_errno(e->name);
            set_minor_error();
            return skip_data_and_pad(opt->fmt, e->filesize);
        }
        if (in_read(target, (size_t)e->filesize) < 0) { free(target); return -1; }
        target[e->filesize] = '\0';
        unlink(full);
        if (symlink(target, full) < 0) { warn_errno(full); set_minor_error(); }
        free(target);
        left = 0;
    } else if (S_ISFIFO(e->mode)) {
        if (mkfifo(full, e->mode & 07777) < 0 && errno != EEXIST) { warn_errno(full); set_minor_error(); }
    } else if (S_ISCHR(e->mode) || S_ISBLK(e->mode)) {
        if (geteuid() != 0) {
            warnx_("%s: device not created: only root may", e->name);
            set_minor_error();
        } else if (mknod(full, e->mode, e->rdev) < 0 && errno != EEXIST) {
            warn_errno(full); set_minor_error();
        }
    } else if (S_ISSOCK(e->mode)) {
        warnx_("%s: socket not created", e->name);
        set_minor_error();
    } else {
        off_t outoff = 0;
        /* Remove any pre-planted symlink at the destination so the write
         * is not redirected outside the extraction tree (archive symlink
         * traversal); O_NOFOLLOW guards hosts whose open(2) honours it. */
        unlink(full);
        fd = open(full, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, e->mode & 07777);
        if (fd < 0) {
            warn_errno(full);
            set_minor_error();
            return skip_data_and_pad(opt->fmt, e->filesize);
        }
        while (left > 0) {
            size_t chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
            if (in_read(buf, chunk) < 0) { close(fd); return -1; }
            left -= chunk;
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
    }

    if (in_skip(left + data_pad(opt->fmt, e->filesize)) < 0) return -1;

    if (!opt->toc) {
        if (opt->numeric_owner && geteuid() == 0) {
            if (lchown(full, e->uid, e->gid) < 0) set_minor_error();
        }
        if (opt->preserve_mtime && !S_ISLNK(e->mode)) {
            struct timespec ts[2];
            ts[0].tv_sec = e->mtime; ts[0].tv_nsec = 0;
            ts[1].tv_sec = e->mtime; ts[1].tv_nsec = 0;
            if (utimensat(AT_FDCWD, full, ts, AT_SYMLINK_NOFOLLOW) < 0) set_minor_error();
        }
        if (opt->verbose) fprintf(stderr, "%s\n", e->name);
    }
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

static int mode_out(options_t *opt) {
    int afd = (opt->archive_file && strcmp(opt->archive_file, "-") != 0)
              ? open(opt->archive_file, O_WRONLY | O_CREAT | O_TRUNC, 0666)
              : STDOUT_FILENO;
    char line[PATH_MAX];
    if (afd < 0) { warn_errno(opt->archive_file); return 2; }
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;
        write_entry(opt, afd, line);
    }
    if (write_trailer(opt, afd) < 0) {
        warn_errno("write error");
        set_minor_error();
    }
    hl_free();
    if (afd != STDOUT_FILENO) close(afd);
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

    if (afd != STDIN_FILENO) close(afd);
    return g_status;
}

static int copy_one_path(const char *src, const char *dst, const options_t *opt) {
    struct stat st;
    char buf[IO_BUFSZ];
    char target[PATH_MAX];
    int in = -1, out = -1;
    if (lstat(src, &st) < 0) { warn_errno(src); set_minor_error(); return -1; }
    if (snprintf(target, sizeof(target), "%s/%s", dst, src) >= (int)sizeof(target)) {
        set_minor_error(); return -1;
    }
    if (opt->make_dirs && make_parent_dirs(target) < 0) { warn_errno(target); set_minor_error(); return -1; }

    if (S_ISDIR(st.st_mode)) {
        if (mkdir(target, st.st_mode & 07777) < 0 && errno != EEXIST) { warn_errno(target); set_minor_error(); }
        return 0;
    }
    if (S_ISLNK(st.st_mode)) {
        ssize_t n = readlink(src, buf, sizeof(buf) - 1);
        if (n < 0) { warn_errno(src); set_minor_error(); return -1; }
        buf[n] = '\0';
        unlink(target);
        if (symlink(buf, target) < 0) { warn_errno(target); set_minor_error(); }
        return 0;
    }
    if (!S_ISREG(st.st_mode)) return 0;

    in = open(src, O_RDONLY);
    out = open(target, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (in < 0 || out < 0) { warn_errno(in < 0 ? src : target); set_minor_error(); goto done; }
    for (;;) {
        ssize_t r = read(in, buf, sizeof(buf));
        if (r < 0) { if (errno == EINTR) continue; warn_errno(src); set_minor_error(); break; }
        if (r == 0) break;
        if (write_all(out, buf, (size_t)r) < 0) { warn_errno(target); set_minor_error(); break; }
    }
    if (opt->preserve_mtime) {
        struct timespec ts[2];
        ts[0].tv_sec = st.st_atime; ts[0].tv_nsec = 0;
        ts[1].tv_sec = st.st_mtime; ts[1].tv_nsec = 0;
        utimensat(AT_FDCWD, target, ts, 0);
    }
done:
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    return 0;
}

static int mode_pass(options_t *opt) {
    char line[PATH_MAX];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;
        copy_one_path(line, opt->pass_dir, opt);
        if (opt->verbose) fprintf(stderr, "%s\n", line);
    }
    return g_status;
}

int main(int argc, char **argv) {
    options_t opt;
    int c;
    char **new_argv = malloc((argc + 1) * sizeof(char *));
    int new_argc = 0;

    if (!new_argv) return 2;

    setlocale(LC_ALL, "");
    memset(&opt, 0, sizeof(opt));
    opt.no_absolute_paths = true;
    opt.safe_extract = true;
    opt.fmt = (fmt_t)-1;

    new_argv[new_argc++] = argv[0];
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--safe-extract") == 0) opt.safe_extract = true;
        else if (strcmp(argv[i], "--no-absolute-paths") == 0) opt.no_absolute_paths = true;
        else if (strcmp(argv[i], "--absolute-paths") == 0) opt.no_absolute_paths = false;
        else if (strcmp(argv[i], "--insecure") == 0) {
            opt.safe_extract = false;
            opt.no_absolute_paths = false;
        } else if (strcmp(argv[i], "--no-overwrite") == 0) opt.no_overwrite = true;
        else if (strcmp(argv[i], "--numeric-owner") == 0) opt.numeric_owner = true;
        else if (strncmp(argv[i], "--format=", 9) == 0) {
            const char *f = argv[i] + 9;
            if (strcmp(f, "newc") == 0) opt.fmt = FMT_NEWC;
            else if (strcmp(f, "odc") == 0) opt.fmt = FMT_ODC;
            else if (strcmp(f, "bin") == 0) opt.fmt = FMT_BIN;
            else { warnx_("unknown archive format: %s", f); free(new_argv); return 2; }
        } else {
            new_argv[new_argc++] = argv[i];
        }
    }
    new_argv[new_argc] = NULL;

    opterr = 0;
    while ((c = getopt(new_argc, new_argv, ":oipF:tvnH:C:R:dmu")) != -1) {
        switch (c) {
        case 'o': opt.mode = MODE_OUT; break;
        case 'i': opt.mode = MODE_IN; break;
        case 'p': opt.mode = MODE_PASS; break;
        case 'F': opt.archive_file = optarg; break;
        case 't': opt.toc = true; break;
        case 'v': opt.verbose = true; break;
        case 'n': opt.numeric_ids = true; break;
        case 'H':
            if (strcmp(optarg, "newc") == 0) opt.fmt = FMT_NEWC;
            else if (strcmp(optarg, "odc") == 0) opt.fmt = FMT_ODC;
            else if (strcmp(optarg, "bin") == 0) opt.fmt = FMT_BIN;
            else { warnx_("unknown archive format: %s", optarg); free(new_argv); return 2; }
            break;
        case 'R':
            if (parse_owner(optarg, &opt.owner_uid, &opt.owner_gid) < 0) {
                warnx_("invalid owner: %s", optarg); free(new_argv); return 2;
            }
            opt.owner_set = true;
            break;
        case 'd': opt.make_dirs = true; break;
        case 'm': opt.preserve_mtime = true; break;
        case 'u': opt.force_overwrite = true; break;
        case 'C': break;
        case ':':
            warnx_("option -%c needs an argument", optopt);
            usage(stderr);
            free(new_argv);
            return 2;
        default:
            warnx_("unknown option -%c", optopt);
            usage(stderr);
            free(new_argv);
            return 2;
        }
    }

    if (opt.mode == MODE_NONE) {
        usage(stderr);
        free(new_argv);
        return 2;
    }
    if (opt.mode == MODE_PASS) {
        if (optind >= new_argc) {
            warnx_("-p needs a destination directory");
            free(new_argv);
            return 2;
        }
        opt.pass_dir = new_argv[optind];
    }

    if (opt.fmt == (fmt_t)-1 && opt.mode == MODE_OUT) opt.fmt = FMT_NEWC;

    if (opt.mode == MODE_OUT) {
        int ret = mode_out(&opt);
        free(new_argv);
        return ret;
    }
    if (opt.mode == MODE_IN) {
        int ret = mode_in(&opt);
        free(new_argv);
        return ret;
    }
    int ret = mode_pass(&opt);
    free(new_argv);
    return ret;
}
