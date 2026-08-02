#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "getopt.h"
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#define TAR_BLOCK 512
#define PAX_TYPE 'x'
#define PAX_MAX (1 << 20)   /* cap on a pax extended-header blob (TAR-06) */

struct tar_header {
    char name[100], mode[8], uid[8], gid[8], size[12], mtime[12], chksum[8];
    char typeflag, linkname[100], magic[6], version[2], uname[32], gname[32];
    char devmajor[8], devminor[8], prefix[155], pad[12];
};

enum mode_type { MODE_NONE, MODE_CREATE, MODE_EXTRACT, MODE_LIST, MODE_APPEND, MODE_UPDATE };
enum format_type { FMT_USTAR, FMT_PAX };
enum comp_type { COMP_NONE, COMP_GZIP, COMP_XZ, COMP_BZIP2 };

struct options {
    enum mode_type mode;
    enum format_type format;
    enum comp_type comp;
    const char *archive;
    const char *snapshot;
    bool verbose;
    bool safe_extract;
    bool no_overwrite;
    bool no_same_owner;
    bool preserve_permissions;
    bool keep_directory_symlink;
    int strip_components;
} opt;

struct snap_entry { char *path; time_t mtime; off_t size; };
struct snapshot { struct snap_entry *v; size_t n; };

struct pax_state {
    char *path;
    char *linkpath;
    int64_t size;
    int64_t uid;
    int64_t gid;
    int64_t mtime;
};

static void pax_init(struct pax_state *p) {
    memset(p, 0, sizeof(*p));
    p->size = p->uid = p->gid = p->mtime = -1;
}

static void pax_reset(struct pax_state *p) {
    free(p->path);
    free(p->linkpath);
    pax_init(p);
}

static void die(const char *s) { perror(s); exit(2); }

static int64_t oct2i(const char *s, size_t n) {
    /* GNU base-256: when the top bit of the first byte is set the field is a
     * big-endian binary integer, not ASCII octal.  Decoding it as octal (and
     * "continue"-skipping the non-octal bytes) silently desyncs large
     * size/uid/mtime values (TAR-07). */
    if (n > 0 && (s[0] & 0x80)) {
        uint64_t v = (uint64_t)((unsigned char)s[0] & 0x7f);
        for (size_t i = 1; i < n; i++) v = (v << 8) | (unsigned char)s[i];
        return (int64_t)v;
    }
    int64_t v = 0;
    for (size_t i = 0; i < n && s[i]; i++) {
        if (s[i] < '0' || s[i] > '7') continue;
        v = (v << 3) + (s[i] - '0');
    }
    return v;
}

static int i2oct(char *d, size_t n, uint64_t v) {
    uint64_t max = 1;
    for (size_t i = 0; i < n - 1; i++) max <<= 3;
    if (v >= max) return -1;
    d[n - 1] = '\0';
    for (size_t i = n - 1; i > 0; i--) {
        d[i - 1] = '0' + (v & 7);
        v >>= 3;
    }
    return 0;
}

static bool all_zero(const unsigned char *b, size_t n) {
    for (size_t i = 0; i < n; i++) if (b[i]) return false;
    return true;
}

static void checksum(struct tar_header *h) {
    memset(h->chksum, ' ', 8);
    unsigned sum = 0;
    unsigned char *p = (unsigned char *)h;
    for (size_t i = 0; i < sizeof(*h); i++) sum += p[i];
    snprintf(h->chksum, sizeof(h->chksum), "%06o", sum);
    h->chksum[6] = '\0'; h->chksum[7] = ' ';
}

static bool verify_checksum(const struct tar_header *h) {
    struct tar_header t = *h;
    checksum(&t);
    return memcmp(t.chksum, h->chksum, 7) == 0;
}

static pid_t arc_pid = -1;

static int split_name(const char *path, char name[100], char prefix[155]) {
    memset(name, 0, 100); memset(prefix, 0, 155);
    size_t len = strlen(path);
    if (len <= 100) { memcpy(name, path, len); return 0; }
    const char *slash = strrchr(path, '/');
    if (!slash) return -1;
    size_t pre = (size_t)(slash - path), n = len - pre - 1;
    if (pre > 155 || n > 100) return -1;
    memcpy(prefix, path, pre); memcpy(name, slash + 1, n);
    return 0;
}

static int wr(FILE *f, const void *p, size_t n) { return fwrite(p, 1, n, f) == n ? 0 : -1; }
static int rd(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n ? 0 : -1; }

static FILE *open_write(bool *pipep) {
    *pipep = false;
    if (!opt.archive || strcmp(opt.archive, "-") == 0) return stdout;
    if (opt.comp == COMP_NONE) return fopen(opt.archive, "wb");

    int pfd[2];
    if (pipe(pfd) < 0) return NULL;

    arc_pid = fork();
    if (arc_pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return NULL;
    }

    if (arc_pid == 0) {
        close(pfd[1]);
        if (dup2(pfd[0], STDIN_FILENO) < 0) {
            perror("tar: dup2 (stdin)"); exit(1);
        }
        close(pfd[0]);

        int out_fd = open(opt.archive, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out_fd < 0) {
            fprintf(stderr, "tar: open %s: %s\n",
                    opt.archive, strerror(errno));
            exit(1);
        }
        if (dup2(out_fd, STDOUT_FILENO) < 0) {
            perror("tar: dup2 (stdout)"); exit(1);
        }
        close(out_fd);

        const char *prog = opt.comp == COMP_GZIP ? "gzip" : (opt.comp == COMP_XZ ? "xz" : "bzip2");
        execlp(prog, prog, "-c", NULL);
        if (opt.comp == COMP_GZIP) execl("/usr/bin/gzip", "gzip", "-c", NULL);
        fprintf(stderr, "tar: exec %s -c: %s\n", prog, strerror(errno));
        exit(127);
    }

    close(pfd[0]);
    *pipep = true;
    return fdopen(pfd[1], "w");
}

static FILE *open_read(bool *pipep) {
    *pipep = false;
    if (!opt.archive || strcmp(opt.archive, "-") == 0) return stdin;
    if (opt.comp == COMP_NONE) return fopen(opt.archive, "rb");

    int pfd[2];
    if (pipe(pfd) < 0) return NULL;

    arc_pid = fork();
    if (arc_pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return NULL;
    }

    if (arc_pid == 0) {
        close(pfd[0]);
        if (dup2(pfd[1], STDOUT_FILENO) < 0) {
            perror("tar: dup2 (stdout)"); exit(1);
        }
        close(pfd[1]);

        int in_fd = open(opt.archive, O_RDONLY);
        if (in_fd < 0) {
            fprintf(stderr, "tar: open %s: %s\n",
                    opt.archive, strerror(errno));
            exit(1);
        }
        if (dup2(in_fd, STDIN_FILENO) < 0) {
            perror("tar: dup2 (stdin)"); exit(1);
        }
        close(in_fd);

        const char *prog = opt.comp == COMP_GZIP ? "gzip" : (opt.comp == COMP_XZ ? "xz" : "bzip2");
        execlp(prog, prog, "-dc", NULL);
        /* execlp returned — couldn't find the decompressor.  Try the
         * conventional absolute path before giving up; PATH might
         * not include /usr/bin in some contexts.  */
        if (opt.comp == COMP_GZIP) execl("/usr/bin/gzip", "gzip", "-dc", NULL);
        fprintf(stderr, "tar: exec %s -dc: %s\n", prog, strerror(errno));
        exit(127);
    }

    close(pfd[1]);
    *pipep = true;
    return fdopen(pfd[0], "r");
}

static void close_arc(FILE *f, bool pipep) {
    if (!f || f == stdin || f == stdout) return;
    fclose(f);
    if (pipep && arc_pid > 0) {
        int wst = 0;
        waitpid(arc_pid, &wst, 0);
        if (WIFEXITED(wst) && WEXITSTATUS(wst) != 0)
            fprintf(stderr, "tar: decompressor exited with status %d\n",
                    WEXITSTATUS(wst));
        else if (WIFSIGNALED(wst))
            fprintf(stderr, "tar: decompressor killed by signal %d\n",
                    WTERMSIG(wst));
    }
}

static int pax_append(char **buf, size_t *len, const char *k, const char *v) {
    char body[4096];
    int m = snprintf(body, sizeof(body), "%s=%s\n", k, v);
    if (m <= 0 || m >= (int)sizeof(body)) return -1;
    int d = 1, r;
    while (1) {
        r = d + 1 + m;
        int nd = 1; for (int t = r; t >= 10; t /= 10) nd++;
        if (nd == d) break;
        d = nd;
    }
    char line[8192];
    int n = snprintf(line, sizeof(line), "%d %s", r, body);
    if (n != r) return -1;
    char *p = realloc(*buf, *len + (size_t)n);
    if (!p) return -1;
    *buf = p;
    memcpy(*buf + *len, line, (size_t)n);
    *len += (size_t)n;
    return 0;
}

static int emit_header(FILE *out, const char *path, const struct stat *st, char type, const char *link, int64_t size);

static int emit_pax(FILE *out, const struct stat *st, const struct pax_state *ps) {
    char *blob = NULL;
    size_t len = 0;
    char num[64];
    if (ps->path && pax_append(&blob, &len, "path", ps->path)) goto fail;
    if (ps->linkpath && pax_append(&blob, &len, "linkpath", ps->linkpath)) goto fail;
    if (ps->size >= 0) { snprintf(num, sizeof(num), "%lld", (long long)ps->size); if (pax_append(&blob, &len, "size", num)) goto fail; }
    if (ps->uid >= 0) { snprintf(num, sizeof(num), "%lld", (long long)ps->uid); if (pax_append(&blob, &len, "uid", num)) goto fail; }
    if (ps->gid >= 0) { snprintf(num, sizeof(num), "%lld", (long long)ps->gid); if (pax_append(&blob, &len, "gid", num)) goto fail; }
    if (ps->mtime >= 0) { snprintf(num, sizeof(num), "%lld", (long long)ps->mtime); if (pax_append(&blob, &len, "mtime", num)) goto fail; }

    struct stat fake = *st;
    fake.st_mode = 0644;
    if (emit_header(out, "PaxHeaders.0/entry", &fake, PAX_TYPE, NULL, (int64_t)len)) goto fail;
    if (wr(out, blob, len)) goto fail;
    size_t pad = (TAR_BLOCK - (len % TAR_BLOCK)) % TAR_BLOCK;
    if (pad) { unsigned char z[TAR_BLOCK] = {0}; if (wr(out, z, pad)) goto fail; }
    free(blob);
    return 0;
fail:
    free(blob);
    return -1;
}

static int emit_header(FILE *out, const char *path, const struct stat *st, char type, const char *link, int64_t size) {
    struct tar_header h;
    memset(&h, 0, sizeof(h));

    struct pax_state ps; pax_init(&ps);
    bool need_pax = false;
    if (split_name(path, h.name, h.prefix)) need_pax = true;
    if (i2oct(h.mode, sizeof(h.mode), st->st_mode & 07777)) need_pax = true;
    if (i2oct(h.uid, sizeof(h.uid), st->st_uid)) need_pax = true;
    if (i2oct(h.gid, sizeof(h.gid), st->st_gid)) need_pax = true;
    if (i2oct(h.size, sizeof(h.size), size < 0 ? 0 : (uint64_t)size)) need_pax = true;
    if (i2oct(h.mtime, sizeof(h.mtime), st->st_mtime)) need_pax = true;

    if (need_pax && opt.format == FMT_USTAR) {
        fprintf(stderr, "tar: cannot represent metadata in ustar: %s\n", path);
        pax_reset(&ps);
        return -1;
    }
    if (need_pax && opt.format == FMT_PAX) {
        ps.path = strdup(path);
        ps.size = size;
        ps.uid = st->st_uid;
        ps.gid = st->st_gid;
        ps.mtime = st->st_mtime;
        if (emit_pax(out, st, &ps)) { pax_reset(&ps); return -1; }
        memset(&h, 0, sizeof(h));
        split_name("entry", h.name, h.prefix);
        i2oct(h.mode, sizeof(h.mode), st->st_mode & 07777);
        i2oct(h.uid, sizeof(h.uid), st->st_uid);
        i2oct(h.gid, sizeof(h.gid), st->st_gid);
        i2oct(h.size, sizeof(h.size), size < 0 ? 0 : (uint64_t)size);
        i2oct(h.mtime, sizeof(h.mtime), st->st_mtime);
    }

    h.typeflag = type;
    if (link) strlcpy(h.linkname, link, sizeof(h.linkname));
    memcpy(h.magic, "ustar", 5);
    memcpy(h.version, "00", 2);
    checksum(&h);
    pax_reset(&ps);
    return wr(out, &h, sizeof(h));
}

static bool snap_changed(const struct snapshot *s, const char *p, const struct stat *st) {
    if (!s || !s->v) return true;
    for (size_t i = 0; i < s->n; i++) if (strcmp(s->v[i].path, p) == 0)
        return s->v[i].mtime != st->st_mtime || s->v[i].size != st->st_size;
    return true;
}

static int snap_add(struct snapshot *s, const char *p, const struct stat *st) {
    struct snap_entry *n = realloc(s->v, (s->n + 1) * sizeof(*n));
    if (!n) return -1;
    s->v = n;
    s->v[s->n].path = strdup(p);
    s->v[s->n].mtime = st->st_mtime;
    s->v[s->n].size = st->st_size;
    s->n++;
    return 0;
}

static int load_snapshot(struct snapshot *s) {
    if (!opt.snapshot) return 0;
    FILE *f = fopen(opt.snapshot, "r");
    if (!f) return errno == ENOENT ? 0 : -1;
    char line[4096], p[3072];
    long long mt, sz;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%3071[^\t]\t%lld\t%lld", p, &mt, &sz) == 3) {
            struct stat st; memset(&st, 0, sizeof(st)); st.st_mtime = (time_t)mt; st.st_size = (off_t)sz;
            if (snap_add(s, p, &st)) { fclose(f); return -1; }
        }
    }
    fclose(f);
    return 0;
}

static int save_snapshot(const struct snapshot *s) {
    if (!opt.snapshot) return 0;
    FILE *f = fopen(opt.snapshot, "w");
    if (!f) return -1;
    for (size_t i = 0; i < s->n; i++)
        fprintf(f, "%s\t%lld\t%lld\n", s->v[i].path, (long long)s->v[i].mtime, (long long)s->v[i].size);
    fclose(f);
    return 0;
}

static int write_file_data(FILE *out, int fd, off_t size) {
    unsigned char buf[32768];
    off_t rem = size;
    while (rem > 0) {
        size_t n = rem > (off_t)sizeof(buf) ? sizeof(buf) : (size_t)rem;
        ssize_t r = read(fd, buf, n);
        if (r <= 0) return -1;
        if (wr(out, buf, (size_t)r)) return -1;
        rem -= r;
    }
    size_t pad = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
    if (pad) { unsigned char z[TAR_BLOCK] = {0}; if (wr(out, z, pad)) return -1; }
    return 0;
}

static int archive_path(FILE *out, const char *path, const struct snapshot *old, struct snapshot *new) {
    struct stat st;
    if (lstat(path, &st)) return -1;
    if (snap_add(new, path, &st)) return -1;
    bool include = (opt.mode != MODE_UPDATE) || snap_changed(old, path, &st);

    if (S_ISDIR(st.st_mode)) {
        char d[PATH_MAX]; snprintf(d, sizeof(d), "%s/", path);
        if (include && emit_header(out, d, &st, '5', NULL, 0)) return -1;
        DIR *dp = opendir(path);
        if (!dp) return -1;

        char c[PATH_MAX];
        size_t path_len = strlen(path);
        size_t base_len = path_len < sizeof(c) - 1 ? path_len : sizeof(c) - 1;
        memcpy(c, path, base_len);
        if (base_len < sizeof(c) - 1) c[base_len++] = '/';
        char *name_ptr = c + base_len;
        size_t rem = sizeof(c) - base_len;

        struct dirent *de;
        while ((de = readdir(dp))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            size_t name_len = strlen(de->d_name);
            size_t copy_len = name_len < rem - 1 ? name_len : rem - 1;
            memcpy(name_ptr, de->d_name, copy_len);
            name_ptr[copy_len] = '\0';
            if (archive_path(out, c, old, new)) { closedir(dp); return -1; }
        }
        closedir(dp);
        return 0;
    }
    if (!include) return 0;
    if (S_ISREG(st.st_mode)) {
        if (emit_header(out, path, &st, '0', NULL, st.st_size)) return -1;
        int fd = open(path, O_RDONLY);
        if (fd < 0) return -1;
        int rc = write_file_data(out, fd, st.st_size);
        close(fd);
        return rc;
    }
    if (S_ISLNK(st.st_mode)) {
        char l[PATH_MAX]; ssize_t n = readlink(path, l, sizeof(l) - 1);
        if (n < 0) return -1;
        l[n] = 0;
        return emit_header(out, path, &st, '2', l, 0);
    }
    if (S_ISFIFO(st.st_mode)) return emit_header(out, path, &st, '6', NULL, 0);
    return 0;
}

static int write_eoa(FILE *out) {
    unsigned char z[TAR_BLOCK] = {0};
    return wr(out, z, TAR_BLOCK) || wr(out, z, TAR_BLOCK) ? -1 : 0;
}

static char *map_extract_path(const char *in) {
    while (*in == '/') in++;
    char *dup = strdup(in);
    if (!dup) return NULL;
    char *p = dup;
    for (int i = 0; i < opt.strip_components; i++) {
        char *s = strchr(p, '/');
        if (!s) { free(dup); return strdup("."); }
        p = s + 1;
    }
    char *out = strdup(p); free(dup);
    if (!out) return NULL;
    if (strstr(out, "../") || strstr(out, "/..") || !strcmp(out, "..") || in[0] == '/') {
        free(out); return NULL;
    }
    return out;
}

/*
 * Open the directory that will hold `relpath`'s final component, walking one
 * component at a time from the pinned extraction-root fd with
 * O_DIRECTORY|O_NOFOLLOW so that NO symlink in any intermediate component can
 * redirect the operation outside the extraction root (TAR-01).  Intermediate
 * directories are created with mkdirat.  On success returns a dirfd the caller
 * must close and copies the final path component into leaf[leafsz].  `relpath`
 * must already be root-relative and free of "../" (map_extract_path).
 */
static int open_parent_dir(int rootfd, const char *relpath, char *leaf, size_t leafsz)
{
    int dirfd = dup(rootfd);
    const char *p = relpath;
    if (dirfd < 0) return -1;
    while (*p == '/') p++;
    for (;;) {
        const char *slash = strchr(p, '/');
        if (!slash) {
            if (*p == '\0' || !strcmp(p, ".") || !strcmp(p, "..")) {
                close(dirfd); errno = EINVAL; return -1;
            }
            if (strlcpy(leaf, p, leafsz) >= leafsz) {
                close(dirfd); errno = ENAMETOOLONG; return -1;
            }
            return dirfd;
        }
        size_t clen = (size_t)(slash - p);
        if (clen == 0) { p = slash + 1; continue; }   /* collapse // */
        if (clen > NAME_MAX) { close(dirfd); errno = ENAMETOOLONG; return -1; }
        char comp[NAME_MAX + 1];
        memcpy(comp, p, clen); comp[clen] = '\0';
        if (!strcmp(comp, "..")) { close(dirfd); errno = EINVAL; return -1; }
        if (strcmp(comp, ".") != 0) {
            int nfd;
            if (mkdirat(dirfd, comp, 0777) && errno != EEXIST) { close(dirfd); return -1; }
            nfd = openat(dirfd, comp, O_DIRECTORY | O_NOFOLLOW | O_RDONLY);
            if (nfd < 0) { close(dirfd); return -1; }   /* symlink component => ELOOP */
            close(dirfd); dirfd = nfd;
        }
        p = slash + 1;
    }
}

static int extract_reg_at(FILE *in, int dirfd, const char *leaf, off_t size) {
    int fd = -1;
    if (!(opt.no_overwrite && faccessat(dirfd, leaf, F_OK, 0) == 0)) {
        /* Remove any existing entry (a pre-planted symlink from an earlier
         * member) then create with O_NOFOLLOW so the final component is never
         * followed either — the classic archive symlink traversal. */
        unlinkat(dirfd, leaf, 0);
        fd = openat(dirfd, leaf, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0666);
        if (fd < 0) return -1;
    }
    unsigned char b[TAR_BLOCK];
    off_t rem = size;
    while (rem > 0) {
        size_t take = rem > TAR_BLOCK ? TAR_BLOCK : (size_t)rem;
        if (rd(in, b, TAR_BLOCK)) { if (fd >= 0) close(fd); return -1; }
        if (fd >= 0) {
            if (all_zero(b, take)) {
                if (lseek(fd, (off_t)take, SEEK_CUR) < 0 && write(fd, b, take) != (ssize_t)take) { close(fd); return -1; }
            } else if (write(fd, b, take) != (ssize_t)take) { close(fd); return -1; }
        }
        rem -= take;
    }
    if (fd >= 0) { if (ftruncate(fd, size)) { close(fd); return -1; } close(fd); }
    return 0;
}

static int pax_parse(const char *blob, size_t len, struct pax_state *ps) {
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && blob[j] != ' ') j++;
        if (j >= len) break;
        int rec = atoi(blob + i);
        if (rec <= 0 || i + (size_t)rec > len) return -1;
        /*
         * The record is "<len> <key>=<value>\n"; the "<len> " prefix is
         * (j+1-i) bytes.  A record whose declared length is <= the prefix
         * (e.g. "1 =\n") would make n = rec - prefix underflow to a huge
         * size_t and memchr/strndup over-read far past the blob (TAR-03).
         * Require room for the prefix plus at least "=\n".
         */
        size_t plen = (size_t)(j + 1 - i);
        if ((size_t)rec <= plen || (size_t)rec - plen < 2) return -1;
        const char *s = blob + j + 1;
        size_t n = (size_t)rec - plen;
        const char *eq = memchr(s, '=', n);
        if (!eq) return -1;
        size_t klen = (size_t)(eq - s);
        if (n < klen + 2) return -1;            /* guard vlen underflow */
        size_t vlen = n - klen - 2;
        char key[64];
        size_t kcopy = klen >= sizeof(key) ? sizeof(key) - 1 : klen;
        memcpy(key, s, kcopy); key[kcopy] = 0;
        char *v = strndup(eq + 1, vlen);
        if (!v) return -1;
        if (!strcmp(key, "path")) { free(ps->path); ps->path = v; }
        else if (!strcmp(key, "linkpath")) { free(ps->linkpath); ps->linkpath = v; }
        else if (!strcmp(key, "size")) { ps->size = atoll(v); free(v); }
        else if (!strcmp(key, "uid")) { ps->uid = atoll(v); free(v); }
        else if (!strcmp(key, "gid")) { ps->gid = atoll(v); free(v); }
        else if (!strcmp(key, "mtime")) { ps->mtime = atoll(v); free(v); }
        else free(v);
        i += (size_t)rec;
    }
    return 0;
}

static int skip_padded(FILE *in, int64_t sz) {
    off_t skip = (off_t)(((sz + TAR_BLOCK - 1) / TAR_BLOCK) * TAR_BLOCK);
    unsigned char b[TAR_BLOCK];
    while (skip > 0) { if (rd(in, b, TAR_BLOCK)) return -1; skip -= TAR_BLOCK; }
    return 0;
}

static int process_read(FILE *in, bool extract) {
    struct pax_state ps; pax_init(&ps);
    int zeros = 0;
    int dbg = getenv("TAR_DEBUG") != NULL;
    long total_read = 0;
    /* Pin the extraction root (cwd, after any -C).  Every member is written
     * relative to this fd via a per-component O_NOFOLLOW walk so no symlink
     * planted by an earlier member can redirect a write outside it. */
    int rootfd = -1;
    if (extract) {
        rootfd = open(".", O_RDONLY | O_DIRECTORY);
        if (rootfd < 0) { pax_reset(&ps); return -1; }
    }
    while (1) {
        struct tar_header h;
        if (dbg) fprintf(stderr, "tar: reading header (total=%ld)\n", total_read);
        if (rd(in, &h, sizeof(h))) {
            if (dbg) fprintf(stderr, "tar: header read failed/short\n");
            break;
        }
        total_read += sizeof(h);
        if (all_zero((unsigned char *)&h, sizeof(h))) {
            if (dbg) fprintf(stderr, "tar: zero block %d\n", zeros + 1);
            if (++zeros == 2) break;
            continue;
        }
        zeros = 0;
        if (!verify_checksum(&h)) return -1;

        int64_t hsize = oct2i(h.size, sizeof(h.size));
        /* name/prefix/linkname are fixed-width fields with no guaranteed NUL;
         * bound every read with a field-width precision / explicit copy so a
         * full-width value can't read past the field (TAR-04). */
        char name[PATH_MAX];
        if (h.prefix[0]) snprintf(name, sizeof(name), "%.155s/%.100s", h.prefix, h.name);
        else snprintf(name, sizeof(name), "%.100s", h.name);
        char linkbuf[sizeof(h.linkname) + 1];
        memcpy(linkbuf, h.linkname, sizeof(h.linkname));
        linkbuf[sizeof(h.linkname)] = '\0';

        if (h.typeflag == PAX_TYPE) {
            /* Cap the extended-header size: reject non-positive or absurd
             * values before the (size_t) cast so a 64-bit attacker length
             * can't truncate to a small/zero malloc that then desyncs the
             * stream (TAR-06). */
            if (hsize <= 0 || hsize > PAX_MAX) return -1;
            char *blob = malloc((size_t)hsize);
            if (!blob) return -1;
            if (fread(blob, 1, (size_t)hsize, in) != (size_t)hsize) { free(blob); return -1; }
            size_t pad = (TAR_BLOCK - ((size_t)hsize % TAR_BLOCK)) % TAR_BLOCK;
            if (pad) { unsigned char t[TAR_BLOCK]; if (rd(in, t, pad)) { free(blob); return -1; } }
            if (pax_parse(blob, (size_t)hsize, &ps)) { free(blob); return -1; }
            free(blob);
            continue;
        }

        int64_t size = ps.size >= 0 ? ps.size : hsize;
        const char *entry = ps.path ? ps.path : name;
        if (opt.mode == MODE_LIST) {
            if (opt.verbose) printf("%c %10lld %s\n", h.typeflag ? h.typeflag : '0', (long long)size, entry);
            else puts(entry);
        }

        if (!extract) {
            if (skip_padded(in, hsize)) return -1;
            pax_reset(&ps);
            continue;
        }

        char tf = h.typeflag ? h.typeflag : '0';
        char *target = map_extract_path(entry);
        if (!target) {
            fprintf(stderr, "tar: unsafe path rejected: %s\n", entry);
            if (tf != '5' && skip_padded(in, hsize)) { close(rootfd); return -1; }
            pax_reset(&ps); continue;
        }

        /* Trim a directory member's trailing '/', and skip the archive's
         * "." / "" root entry — it needs no creation and has no leaf. */
        size_t tl = strlen(target);
        while (tl > 1 && target[tl - 1] == '/') target[--tl] = '\0';
        if (!strcmp(target, ".") || target[0] == '\0') {
            free(target);
            if (tf != '5' && skip_padded(in, hsize)) { close(rootfd); return -1; }
            pax_reset(&ps); continue;
        }

        char leaf[NAME_MAX + 1];
        int pdir = open_parent_dir(rootfd, target, leaf, sizeof(leaf));
        if (pdir < 0) {
            fprintf(stderr, "tar: %s: %s\n", target, strerror(errno));
            free(target);
            if (tf != '5' && skip_padded(in, hsize)) { close(rootfd); return -1; }
            pax_reset(&ps); continue;
        }

        switch (tf) {
            case '5':
                if (mkdirat(pdir, leaf, 0777) && errno != EEXIST) perror(target);
                break;
            case '2': {   /* symlink: leaf itself may be replaced, never followed */
                const char *lt = ps.linkpath ? ps.linkpath : linkbuf;
                if (!opt.no_overwrite || faccessat(pdir, leaf, F_OK, AT_SYMLINK_NOFOLLOW)) {
                    unlinkat(pdir, leaf, 0);
                    if (symlinkat(lt, pdir, leaf)) perror(target);
                }
                if (skip_padded(in, hsize)) { close(pdir); free(target); close(rootfd); return -1; }
                break;
            }
            case '1': {   /* hardlink: confine the source to the extraction root (TAR-02) */
                const char *lt = ps.linkpath ? ps.linkpath : linkbuf;
                /*
                 * The old code passed linkname to link() verbatim, so a member
                 * with linkname=/etc/shadow created a hardlink to the victim
                 * inode which the following chmod/chown then rewrote (root
                 * priv-esc).  Reject absolute / ".." link sources via the same
                 * path mapper used for member names, and prove — with an
                 * O_NOFOLLOW component walk — that neither the source nor the
                 * destination path contains a symlink component before calling
                 * link() (there is no native linkat to do this fd-relative).
                 */
                char *src = map_extract_path(lt);
                if (!src) {
                    fprintf(stderr, "tar: unsafe hardlink target rejected: %s\n", lt);
                } else {
                    char sleaf[NAME_MAX + 1];
                    int sdir = open_parent_dir(rootfd, src, sleaf, sizeof(sleaf));
                    if (sdir < 0) { perror(src); }
                    else {
                        close(sdir);           /* walk succeeded: no symlink components */
                        if (!opt.no_overwrite || faccessat(pdir, leaf, F_OK, AT_SYMLINK_NOFOLLOW)) {
                            unlinkat(pdir, leaf, 0);
                            if (link(src, target)) perror(target);
                        }
                    }
                    free(src);
                }
                if (skip_padded(in, hsize)) { close(pdir); free(target); close(rootfd); return -1; }
                break;
            }
            default:
                if (extract_reg_at(in, pdir, leaf, size)) perror(target);
                break;
        }

        /* Metadata via *at on the pinned parent — never re-traverses the path
         * and never follows a symlink member (chmod skipped for symlinks).
         * Strip setuid/setgid from a restored mode so a hostile archive can't
         * plant a setuid-root binary (TAR-05). */
        if (opt.preserve_permissions && tf != '2')
            fchmodat(pdir, leaf,
                     (mode_t)oct2i(h.mode, sizeof(h.mode)) & 07777
                         & ~(mode_t)(S_ISUID | S_ISGID),
                     0);
        if (!opt.no_same_owner) {
            fchownat(pdir, leaf,
                     (uid_t)(ps.uid >= 0 ? ps.uid : oct2i(h.uid, sizeof(h.uid))),
                     (gid_t)(ps.gid >= 0 ? ps.gid : oct2i(h.gid, sizeof(h.gid))),
                     AT_SYMLINK_NOFOLLOW);
        }
        struct timespec ts[2] = {{0}};
        ts[0].tv_sec = oct2i(h.mtime, sizeof(h.mtime));
        ts[1].tv_sec = ps.mtime >= 0 ? ps.mtime : oct2i(h.mtime, sizeof(h.mtime));
        utimensat(pdir, leaf, ts, AT_SYMLINK_NOFOLLOW);

        close(pdir);
        free(target);
        pax_reset(&ps);
    }
    pax_reset(&ps);
    if (rootfd >= 0) close(rootfd);
    return 0;
}

static int prepare_append_fd(void) {
    int fd = open(opt.archive, O_RDWR);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); errno = ESPIPE; return -1; }
    off_t off = st.st_size;
    unsigned char b[TAR_BLOCK];
    while (off >= (off_t)TAR_BLOCK) {
        off -= TAR_BLOCK;
        if (pread(fd, b, TAR_BLOCK, off) != TAR_BLOCK) break;
        if (!all_zero(b, TAR_BLOCK)) { off += TAR_BLOCK; break; }
    }
    if (ftruncate(fd, off) || lseek(fd, off, SEEK_SET) < 0) { close(fd); return -1; }
    return fd;
}

static void usage(void) {
    puts("usage: tar -cxt[vruf] -f archive [options] [files...]\n"
         "  --format=ustar|pax\n"
         "  --safe-extract --strip-components=N --no-overwrite\n"
         "  --no-same-owner --preserve-permissions --keep-directory-symlink\n"
         "  --listed-incremental=SNAP\n"
         "  -z/--gzip  -J/--xz  -j/--bzip2");
}

int main(int argc, char **argv) {
    memset(&opt, 0, sizeof(opt));
    /* Default to NOT restoring archived ownership: applying an untrusted
     * uid/gid (especially as root) is a footgun / priv-esc vector. Opt in
     * with --same-owner (TAR-05). */
    opt.no_same_owner = true;
    opt.format = FMT_PAX;
    opt.safe_extract = true; /* Enable safe extraction by default */

    /* Traditional tar form: the first arg is a bare option bundle
     * without a leading dash (e.g. `tar xvzf archive.tar.gz file1
     * file2 ...`).  Predates getopt(3) by decades; GNU tar still
     * supports it.  Detect by: argv[1] exists, doesn't start with
     * '-', and contains at least one valid mode character (c, x,
     * t, r, u).  If so, splice in the dash by rewriting argv[1]
     * with a leading '-' into a scratch buffer.  */
    if (argc > 1 && argv[1][0] != '-' &&
        strpbrk(argv[1], "cxtru") != NULL) {
        static char dashed[64];
        snprintf(dashed, sizeof(dashed), "-%s", argv[1]);
        argv[1] = dashed;
    }

    static struct option lo[] = {
        {"file", required_argument, 0, 'f'},
        {"format", required_argument, 0, 1000},
        {"gzip", no_argument, 0, 'z'},
        {"xz", no_argument, 0, 'J'},
        {"bzip2", no_argument, 0, 'j'},
        {"safe-extract", no_argument, 0, 1001},
        {"strip-components", required_argument, 0, 1002},
        {"no-overwrite", no_argument, 0, 1003},
        {"no-same-owner", no_argument, 0, 1004},
        {"same-owner", no_argument, 0, 1009},
        {"preserve-permissions", no_argument, 0, 1005},
        {"keep-directory-symlink", no_argument, 0, 1006},
        {"listed-incremental", required_argument, 0, 1007},
        {"concatenate", no_argument, 0, 1008},
        {"directory", required_argument, 0, 'C'},
        {"help", no_argument, 0, 'h'},
        {0,0,0,0}
    };

    int ch;
    while ((ch = getopt_long(argc, argv, "cxtruvf:zJjC:h", lo, NULL)) != -1) {
        switch (ch) {
            case 'c': opt.mode = MODE_CREATE; break;
            case 'x': opt.mode = MODE_EXTRACT; break;
            case 't': opt.mode = MODE_LIST; break;
            case 'r': opt.mode = MODE_APPEND; break;
            case 'u': opt.mode = MODE_UPDATE; break;
            case 'v': opt.verbose = true; break;
            case 'f': opt.archive = optarg; break;
            case 'z': opt.comp = COMP_GZIP; break;
            case 'J': opt.comp = COMP_XZ; break;
            case 'j': opt.comp = COMP_BZIP2; break;
            case 'C': if (chdir(optarg) != 0) die(optarg); break;
            case 1000:
                if (!strcmp(optarg, "ustar")) opt.format = FMT_USTAR;
                else if (!strcmp(optarg, "pax")) opt.format = FMT_PAX;
                else { fprintf(stderr, "tar: unsupported format: %s\n", optarg); return 2; }
                break;
            case 1001: opt.safe_extract = true; break;
            case 1002: opt.strip_components = atoi(optarg); break;
            case 1003: opt.no_overwrite = true; break;
            case 1004: opt.no_same_owner = true; break;
            case 1009: opt.no_same_owner = false; break;
            case 1005: opt.preserve_permissions = true; break;
            case 1006: opt.keep_directory_symlink = true; break;
            case 1007: opt.snapshot = optarg; break;
            case 1008: opt.mode = MODE_APPEND; break;
            case 'h': usage(); return 0;
            default: usage(); return 2;
        }
    }
    if (opt.mode == MODE_NONE) { usage(); return 2; }

    int fargc = argc - optind;
    char **fargv = argv + optind;

    if (opt.mode == MODE_CREATE || opt.mode == MODE_APPEND || opt.mode == MODE_UPDATE) {
        struct snapshot old = {0}, neu = {0};
        if (load_snapshot(&old)) die("load snapshot");

        bool pipep = false;
        FILE *out = NULL;
        if (opt.mode == MODE_APPEND || opt.mode == MODE_UPDATE) {
            if (opt.comp != COMP_NONE) { fprintf(stderr, "tar: append/update require regular archive file\n"); return 2; }
            int fd = prepare_append_fd();
            if (fd < 0) die("append");
            out = fdopen(fd, "wb");
        } else out = open_write(&pipep);
        if (!out) die("open write");

        if (fargc == 0) { fprintf(stderr, "tar: no files specified\n"); return 2; }
        for (int i = 0; i < fargc; i++) if (archive_path(out, fargv[i], &old, &neu)) die(fargv[i]);
        if (write_eoa(out)) die("write eof");
        close_arc(out, pipep);
        if (save_snapshot(&neu)) die("save snapshot");
        return 0;
    }

    bool pipep = false;
    FILE *in = open_read(&pipep);
    if (!in) die("open read");
    int rc = process_read(in, opt.mode == MODE_EXTRACT);
    close_arc(in, pipep);
    return rc ? 2 : 0;
}
