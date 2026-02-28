#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef major
#define major(dev) ((unsigned)(((dev) >> 8) & 0xff))
#endif
#ifndef minor
#define minor(dev) ((unsigned)((dev) & 0xff))
#endif
#ifndef makedev
#define makedev(ma,mi) ((dev_t)((((ma) & 0xff) << 8) | ((mi) & 0xff)))
#endif

#define IO_BUFSZ 65536

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif

int symlink(const char *target, const char *linkpath);
int mkfifo(const char *pathname, mode_t mode);
int mknod(const char *pathname, mode_t mode, dev_t dev);
int lchown(const char *pathname, uid_t owner, gid_t group);
int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags);

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

static int read_all(int fd, void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r == 0) return -1;
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += (size_t)r;
        n -= (size_t)r;
    }
    return 0;
}

static int make_parent_dirs(const char *path) {
    char tmp[PATH_MAX];
    size_t i;
    if (strlen(path) >= sizeof(tmp)) return -1;
    strcpy(tmp, path);
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

static uint16_t be16(const uint8_t *b) { return (uint16_t)(b[0] << 8 | b[1]); }
static void to_be16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)(v >> 8); b[1] = (uint8_t)(v & 0xff); }

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
    to_be16(hdr + 0, 070707);
    to_be16(hdr + 2, (uint16_t)st->st_dev);
    to_be16(hdr + 4, (uint16_t)st->st_ino);
    to_be16(hdr + 6, (uint16_t)st->st_mode);
    to_be16(hdr + 8, (uint16_t)st->st_uid);
    to_be16(hdr + 10, (uint16_t)st->st_gid);
    to_be16(hdr + 12, (uint16_t)nlink);
    to_be16(hdr + 14, (uint16_t)st->st_rdev);
    to_be16(hdr + 16, (uint16_t)((uint32_t)st->st_mtime >> 16));
    to_be16(hdr + 18, (uint16_t)((uint32_t)st->st_mtime & 0xffff));
    to_be16(hdr + 20, namesz);
    to_be16(hdr + 22, (uint16_t)(filesize >> 16));
    to_be16(hdr + 24, (uint16_t)(filesize & 0xffff));
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

static int write_entry(options_t *opt, int afd, const char *path) {
    struct stat st;
    char lnk[PATH_MAX];
    ssize_t lsz = 0;
    uint32_t data_size = 0;
    uint32_t nlink_hdr = 1;
    int ffd = -1;

    if (lstat(path, &st) < 0) {
        perror(path);
        set_minor_error();
        return -1;
    }

    if (S_ISLNK(st.st_mode)) {
        lsz = readlink(path, lnk, sizeof(lnk) - 1);
        if (lsz < 0) { perror(path); set_minor_error(); return -1; }
        lnk[lsz] = '\0';
        data_size = (uint32_t)lsz;
    } else if (S_ISREG(st.st_mode)) {
        hardlink_ent_t *h = NULL;
        data_size = (uint32_t)st.st_size;
        nlink_hdr = st.st_nlink;
        if (st.st_nlink > 1) {
            h = hl_find(st.st_dev, st.st_ino);
            if (h) data_size = 0;
            else if (hl_add(st.st_dev, st.st_ino, path) < 0) {
                perror("hardlink tracking");
                set_minor_error();
            }
        }
        if (data_size > 0) {
            ffd = open(path, O_RDONLY);
            if (ffd < 0) { perror(path); set_minor_error(); return -1; }
        }
    }

    if (opt->owner_set) {
        st.st_uid = opt->owner_uid;
        st.st_gid = opt->owner_gid;
    }

    if (opt->fmt == FMT_NEWC) {
        if (cpio_write_header_newc(afd, path, &st, data_size, nlink_hdr) < 0) goto ioerr;
    } else if (opt->fmt == FMT_ODC) {
        if (cpio_write_header_odc(afd, path, &st, data_size, nlink_hdr) < 0) goto ioerr;
    } else {
        if (cpio_write_header_bin(afd, path, &st, data_size, nlink_hdr) < 0) goto ioerr;
    }

    if (S_ISLNK(st.st_mode)) {
        if (write_all(afd, lnk, (size_t)lsz) < 0) goto ioerr;
    } else if (S_ISREG(st.st_mode) && data_size > 0) {
        if (copy_out_file_data(afd, ffd, data_size) < 0) goto ioerr;
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
    perror("archive write");
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
    mode_t mode;
    uid_t uid;
    gid_t gid;
    nlink_t nlink;
    time_t mtime;
    uint64_t filesize;
    dev_t rdev;
} entry_t;

static int parse_newc_header(int fd, entry_t *e) {
    char hdr[110 + 1];
    char *name;
    unsigned ino, mode, uid, gid, nlink, mtime, filesize, rmaj, rmin, namesz;
    size_t padded;

    if (read_all(fd, hdr, 110) < 0) return -1;
    hdr[110] = '\0';
    if (strncmp(hdr, "070701", 6) != 0 && strncmp(hdr, "070702", 6) != 0) return -1;
    if (sscanf(hdr + 6,
        "%8x%8x%8x%8x%8x%8x%8x%*8x%*8x%8x%8x%8x%*8x",
        &ino, &mode, &uid, &gid, &nlink, &mtime, &filesize, &rmaj, &rmin, &namesz) != 10) {
        return -1;
    }

    if (namesz > PATH_MAX) return -1;

    name = malloc(namesz + 1);
    if (!name) return -1;
    if (read_all(fd, name, namesz) < 0) { free(name); return -1; }
    name[namesz] = '\0';
    padded = 110 + namesz;
    while (padded % 4) {
        char c;
        if (read_all(fd, &c, 1) < 0) { free(name); return -1; }
        padded++;
    }

    e->name = name;
    e->mode = (mode_t)mode;
    e->uid = (uid_t)uid;
    e->gid = (gid_t)gid;
    e->nlink = (nlink_t)nlink;
    e->mtime = (time_t)mtime;
    e->filesize = filesize;
    e->rdev = makedev(rmaj, rmin);
    return 0;
}

static int parse_odc_header(int fd, entry_t *e) {
    char hdr[76 + 1];
    unsigned long mode, uid, gid, nlink, rdev, namesz, mtime;
    unsigned long long filesize;
    if (read_all(fd, hdr, 76) < 0) return -1;
    hdr[76] = '\0';
    if (strncmp(hdr, "070707", 6) != 0) return -1;
    {
        unsigned long dev_ignored, ino_ignored;
        if (sscanf(hdr + 6, "%6lo%6lo%6lo%6lo%6lo%6lo%6lo%11lo%6lo%11llo",
                   &dev_ignored, &ino_ignored, &mode, &uid, &gid, &nlink, &rdev, &mtime, &namesz, &filesize) != 10)
            return -1;
    }

    if (namesz > PATH_MAX) return -1;

    e->name = malloc(namesz + 1);
    if (!e->name) return -1;
    if (read_all(fd, e->name, namesz) < 0) { free(e->name); return -1; }
    e->name[namesz] = '\0';
    e->mode = (mode_t)mode;
    e->uid = (uid_t)uid;
    e->gid = (gid_t)gid;
    e->nlink = (nlink_t)nlink;
    e->mtime = (time_t)mtime;
    e->filesize = filesize;
    e->rdev = (dev_t)rdev;
    return 0;
}

static int parse_bin_header(int fd, entry_t *e) {
    uint8_t hdr[26];
    uint16_t namesz;
    if (read_all(fd, hdr, 26) < 0) return -1;
    if (be16(hdr) != 070707) return -1;
    namesz = be16(hdr + 20);

    if (namesz > PATH_MAX) return -1;

    e->name = malloc((size_t)namesz + 1);
    if (!e->name) return -1;
    if (read_all(fd, e->name, namesz) < 0) { free(e->name); return -1; }
    e->name[namesz] = '\0';
    if ((26 + namesz) & 1) {
        char pad;
        if (read_all(fd, &pad, 1) < 0) { free(e->name); return -1; }
    }
    e->mode = be16(hdr + 6);
    e->uid = be16(hdr + 8);
    e->gid = be16(hdr + 10);
    e->nlink = be16(hdr + 12);
    e->rdev = be16(hdr + 14);
    e->mtime = (time_t)(((uint32_t)be16(hdr + 16) << 16) | be16(hdr + 18));
    e->filesize = ((uint32_t)be16(hdr + 22) << 16) | be16(hdr + 24);
    return 0;
}

static int skip_data_and_pad(int fd, fmt_t fmt, uint64_t size) {
    char buf[IO_BUFSZ];
    while (size > 0) {
        size_t chunk = size > sizeof(buf) ? sizeof(buf) : (size_t)size;
        if (read_all(fd, buf, chunk) < 0) return -1;
        size -= chunk;
    }
    if (fmt == FMT_NEWC) {
        while (size % 4) {
            char pad;
            if (read_all(fd, &pad, 1) < 0) return -1;
            size++;
        }
    }
    return 0;
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

static int extract_entry(int afd, const entry_t *e, const options_t *opt, const char *base) {
    char full[PATH_MAX];
    int fd = -1;
    uint64_t left = e->filesize;
    char buf[IO_BUFSZ];

    if (strcmp(e->name, "TRAILER!!!") == 0) return 1;
    if (!path_is_safe(e->name, opt)) {
        fprintf(stderr, "cpio: blocked unsafe path: %s\n", e->name);
        set_minor_error();
        return skip_data_and_pad(afd, opt->fmt, e->filesize);
    }

    if (snprintf(full, sizeof(full), "%s/%s", base ? base : ".", e->name) >= (int)sizeof(full)) {
        fprintf(stderr, "cpio: path too long: %s\n", e->name);
        set_minor_error();
        return skip_data_and_pad(afd, opt->fmt, e->filesize);
    }

    if (opt->toc) {
        if (opt->verbose) fprintf(stdout, "%s\n", e->name);
        return skip_data_and_pad(afd, opt->fmt, e->filesize);
    }

    if (opt->no_overwrite && access(full, F_OK) == 0) {
        fprintf(stderr, "cpio: no-overwrite skipped: %s\n", full);
        return skip_data_and_pad(afd, opt->fmt, e->filesize);
    }

    if (opt->make_dirs && make_parent_dirs(full) < 0) {
        perror("mkdir -p");
        set_minor_error();
        return skip_data_and_pad(afd, opt->fmt, e->filesize);
    }

    if (S_ISDIR(e->mode)) {
        if (mkdir(full, e->mode & 07777) < 0 && errno != EEXIST) {
            perror(full); set_minor_error();
        }
    } else if (S_ISLNK(e->mode)) {
        char *target = malloc((size_t)e->filesize + 1);
        if (!target) { set_minor_error(); return -1; }
        if (read_all(afd, target, (size_t)e->filesize) < 0) { free(target); return -1; }
        target[e->filesize] = '\0';
        unlink(full);
        if (symlink(target, full) < 0) { perror(full); set_minor_error(); }
        free(target);
        left = 0;
    } else if (S_ISFIFO(e->mode)) {
        if (mkfifo(full, e->mode & 07777) < 0 && errno != EEXIST) { perror(full); set_minor_error(); }
    } else if (S_ISCHR(e->mode) || S_ISBLK(e->mode)) {
        if (geteuid() != 0) {
            fprintf(stderr, "cpio: skipping device node %s (not root)\n", full);
            set_minor_error();
        } else if (mknod(full, e->mode, e->rdev) < 0 && errno != EEXIST) {
            perror(full); set_minor_error();
        }
    } else if (S_ISSOCK(e->mode)) {
        fprintf(stderr, "cpio: skipping socket %s\n", full);
        set_minor_error();
    } else {
        off_t outoff = 0;
        fd = open(full, O_WRONLY | O_CREAT | O_TRUNC, e->mode & 07777);
        if (fd < 0) {
            perror(full);
            set_minor_error();
            return skip_data_and_pad(afd, opt->fmt, e->filesize);
        }
        while (left > 0) {
            size_t chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
            if (read_all(afd, buf, chunk) < 0) { close(fd); return -1; }
            if (write_sparse_aware(fd, buf, chunk, &outoff) < 0) { close(fd); return -1; }
            left -= chunk;
        }
        if (ftruncate(fd, (off_t)e->filesize) < 0) {
            perror("ftruncate");
            set_minor_error();
        }
        close(fd);
    }

    while (left > 0) {
        size_t chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
        if (read_all(afd, buf, chunk) < 0) return -1;
        left -= chunk;
    }

    if (opt->fmt == FMT_NEWC) {
        uint64_t pad = (4 - (e->filesize % 4)) % 4;
        while (pad--) {
            char c;
            if (read_all(afd, &c, 1) < 0) return -1;
        }
    } else if (opt->fmt == FMT_BIN && (e->filesize & 1)) {
        char c;
        if (read_all(afd, &c, 1) < 0) return -1;
    }

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
        if (opt->verbose) fprintf(stderr, "%s\n", full);
    }
    return 0;
}

static fmt_t detect_fmt(int fd) {
    char b[6];
    if (read_all(fd, b, 6) < 0) return FMT_NEWC;
    if (lseek(fd, 0, SEEK_SET) < 0) return FMT_NEWC;
    if (memcmp(b, "070701", 6) == 0 || memcmp(b, "070702", 6) == 0) return FMT_NEWC;
    if (memcmp(b, "070707", 6) == 0) return FMT_ODC;
    return FMT_BIN;
}

static int mode_out(options_t *opt) {
    int afd = (opt->archive_file && strcmp(opt->archive_file, "-") != 0)
              ? open(opt->archive_file, O_WRONLY | O_CREAT | O_TRUNC, 0666)
              : STDOUT_FILENO;
    char line[PATH_MAX];
    if (afd < 0) { perror("open archive"); return 2; }
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;
        write_entry(opt, afd, line);
    }
    if (write_trailer(opt, afd) < 0) {
        perror("trailer");
        set_minor_error();
    }
    if (afd != STDOUT_FILENO) close(afd);
    return g_status;
}

static int mode_in(options_t *opt) {
    int afd = (opt->archive_file && strcmp(opt->archive_file, "-") != 0)
              ? open(opt->archive_file, O_RDONLY)
              : STDIN_FILENO;
    if (afd < 0) { perror("open archive"); return 2; }

    if (!opt->toc && opt->pass_dir) {
        if (chdir(opt->pass_dir) < 0) { perror("chdir"); return 2; }
    }

    if (opt->fmt == (fmt_t)-1)
        opt->fmt = detect_fmt(afd);

    for (;;) {
        entry_t e;
        int r;
        memset(&e, 0, sizeof(e));
        if (opt->fmt == FMT_NEWC) r = parse_newc_header(afd, &e);
        else if (opt->fmt == FMT_ODC) r = parse_odc_header(afd, &e);
        else r = parse_bin_header(afd, &e);
        if (r < 0) break;
        r = extract_entry(afd, &e, opt, ".");
        free(e.name);
        if (r == 1) break;
        if (r < 0) { set_fatal_error(); break; }
    }

    if (afd != STDIN_FILENO) close(afd);
    return g_status;
}

static int copy_one_path(const char *src, const char *dst, const options_t *opt) {
    struct stat st;
    char buf[IO_BUFSZ];
    char target[PATH_MAX];
    int in = -1, out = -1;
    if (lstat(src, &st) < 0) { perror(src); set_minor_error(); return -1; }
    if (snprintf(target, sizeof(target), "%s/%s", dst, src) >= (int)sizeof(target)) {
        set_minor_error(); return -1;
    }
    if (opt->make_dirs && make_parent_dirs(target) < 0) { perror("mkdir -p"); set_minor_error(); return -1; }

    if (S_ISDIR(st.st_mode)) {
        if (mkdir(target, st.st_mode & 07777) < 0 && errno != EEXIST) { perror(target); set_minor_error(); }
        return 0;
    }
    if (S_ISLNK(st.st_mode)) {
        ssize_t n = readlink(src, buf, sizeof(buf) - 1);
        if (n < 0) { perror(src); set_minor_error(); return -1; }
        buf[n] = '\0';
        unlink(target);
        if (symlink(buf, target) < 0) { perror(target); set_minor_error(); }
        return 0;
    }
    if (!S_ISREG(st.st_mode)) return 0;

    in = open(src, O_RDONLY);
    out = open(target, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (in < 0 || out < 0) { perror("open"); set_minor_error(); goto done; }
    for (;;) {
        ssize_t r = read(in, buf, sizeof(buf));
        if (r < 0) { if (errno == EINTR) continue; perror(src); set_minor_error(); break; }
        if (r == 0) break;
        if (write_all(out, buf, (size_t)r) < 0) { perror(target); set_minor_error(); break; }
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
            else { fprintf(stderr, "unknown format: %s\n", f); free(new_argv); return 2; }
        } else {
            new_argv[new_argc++] = argv[i];
        }
    }
    new_argv[new_argc] = NULL;

    opterr = 0;
    while ((c = getopt(new_argc, new_argv, "oipF:tvH:C:R:dmu")) != -1) {
        switch (c) {
        case 'o': opt.mode = MODE_OUT; break;
        case 'i': opt.mode = MODE_IN; break;
        case 'p': opt.mode = MODE_PASS; break;
        case 'F': opt.archive_file = optarg; break;
        case 't': opt.toc = true; break;
        case 'v': opt.verbose = true; break;
        case 'H':
            if (strcmp(optarg, "newc") == 0) opt.fmt = FMT_NEWC;
            else if (strcmp(optarg, "odc") == 0) opt.fmt = FMT_ODC;
            else if (strcmp(optarg, "bin") == 0) opt.fmt = FMT_BIN;
            else { fprintf(stderr, "unknown format: %s\n", optarg); return 2; }
            break;
        case 'R':
            if (parse_owner(optarg, &opt.owner_uid, &opt.owner_gid) < 0) {
                fprintf(stderr, "invalid owner: %s\n", optarg); return 2;
            }
            opt.owner_set = true;
            break;
        case 'd': opt.make_dirs = true; break;
        case 'm': opt.preserve_mtime = true; break;
        case 'u': opt.force_overwrite = true; break;
        case 'C': break;
        default:
            usage(stderr);
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
            fprintf(stderr, "cpio: -p requires target directory\n");
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
