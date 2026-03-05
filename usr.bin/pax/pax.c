#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef HAVE_SYMLINK_PROTO
int symlink(const char *target, const char *linkpath);
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define TAR_BLOCK 512
#define CPIO_NEWC_MAGIC "070701"

enum mode_kind { MODE_LIST, MODE_READ, MODE_WRITE, MODE_COPY };
enum fmt_kind { FMT_PAX, FMT_USTAR, FMT_CPIO_NEWC };

struct tar_hdr {
    char name[100], mode[8], uid[8], gid[8], size[12], mtime[12], chksum[8];
    char typeflag, linkname[100], magic[6], version[2], uname[32], gname[32];
    char devmajor[8], devminor[8], prefix[155], pad[12];
};

struct subst_rule {
    char oldv[128];
    char newv[128];
    int global;
};

struct opts {
    enum mode_kind mode;
    enum fmt_kind format;
    const char *archive;
    int verbose, follow_links, no_overwrite, list_only;
    int no_abs_paths;
    int preserve_mode, preserve_time, preserve_owner;
    const char *copy_dest;
    int first_nonopt;
    struct subst_rule subs[16];
    size_t sub_count;
};

struct pax_kv {
    char path[PATH_MAX];
    char linkpath[PATH_MAX];
    long uid, gid;
    long long size;
    long mtime;
    int has_path, has_linkpath, has_uid, has_gid, has_size, has_mtime;
};

static void usage(void) {
    fprintf(stderr,
        "usage: pax [-cdnv] [-f archive] [-s replstr] ... [pattern ...]\n"
        "       pax -r [-cdiknuv] [-f archive] [-p string] [-s replstr] ... [pattern ...]\n"
        "       pax -w [-dituvX] [-f archive] [-x format] ... [file ...]\n"
        "       pax -r -w [-diklntuvX] [-p string] [-s replstr] ... [file ...] directory\n"
        "       pax --no-absolute-paths ...\n");
    exit(1);
}

static void die(const char *msg) {
    fprintf(stderr, "pax: %s: %s\n", msg, strerror(errno));
    exit(1);
}

static int has_dotdot(const char *p) {
    const char *s = p;
    while (*s) {
        if ((s == p || s[-1] == '/') && s[0] == '.' && s[1] == '.' && (s[2] == '/' || s[2] == '\0')) return 1;
        s++;
    }
    return 0;
}

static void sanitize_path(const struct opts *o, const char *in, char *out, size_t outsz) {
    const char *p = in;
    while (*p == '/') p++;
    if (!*p) p = ".";
    if (!o->no_abs_paths && in[0] != '/') p = in;
    if (has_dotdot(p)) {
        fprintf(stderr, "pax: refusing unsafe path: %s\n", in);
        out[0] = '\0';
        return;
    }
    snprintf(out, outsz, "%s", p);
}

static void ensure_parent_dirs(const char *path) {
    char tmp[PATH_MAX];
    size_t i;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (i = 1; tmp[i]; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
}

static void parse_subst(struct opts *o, const char *arg) {
    struct subst_rule *r;
    char d;
    const char *p, *q;
    if (o->sub_count >= 16) {
        fprintf(stderr, "pax: too many -s rules\n");
        exit(1);
    }
    d = arg[0];
    if (!d) usage();
    p = arg + 1;
    q = strchr(p, d);
    if (!q) usage();
    r = &o->subs[o->sub_count++];
    snprintf(r->oldv, sizeof(r->oldv), "%.*s", (int)(q - p), p);
    p = q + 1;
    q = strchr(p, d);
    if (!q) usage();
    snprintf(r->newv, sizeof(r->newv), "%.*s", (int)(q - p), p);
    r->global = strchr(q + 1, 'g') != NULL;
}

static void apply_subst(const struct opts *o, char *path, size_t psz) {
    size_t i;
    for (i = 0; i < o->sub_count; i++) {
        const struct subst_rule *r = &o->subs[i];
        char out[PATH_MAX];
        char *src = path;
        char *pos;
        size_t out_len = 0;
        size_t out_capacity = sizeof(out) - 1;
        size_t oldv_len = strlen(r->oldv);
        size_t newv_len = strlen(r->newv);

        while ((pos = strstr(src, r->oldv)) != NULL) {
            size_t copy_len = (size_t)(pos - src);
            if (copy_len > out_capacity - out_len) {
                copy_len = out_capacity - out_len;
            }
            if (copy_len > 0) {
                memcpy(out + out_len, src, copy_len);
                out_len += copy_len;
            }

            size_t replace_len = newv_len;
            if (replace_len > out_capacity - out_len) {
                replace_len = out_capacity - out_len;
            }
            if (replace_len > 0) {
                memcpy(out + out_len, r->newv, replace_len);
                out_len += replace_len;
            }

            src = pos + oldv_len;
            if (!r->global) break;
        }

        size_t rest_len = strlen(src);
        if (rest_len > out_capacity - out_len) {
            rest_len = out_capacity - out_len;
        }
        if (rest_len > 0) {
            memcpy(out + out_len, src, rest_len);
            out_len += rest_len;
        }
        out[out_len] = '\0';

        snprintf(path, psz, "%s", out);
    }
}

static void parse_preserve(struct opts *o, const char *p) {
    while (*p) {
        if (*p == 'm') o->preserve_mode = 1;
        else if (*p == 't' || *p == 'e') o->preserve_time = 1;
        else if (*p == 'o') o->preserve_owner = 1;
        p++;
    }
}

static void parse_opts(int argc, char **argv, struct opts *o) {
    int c;
    memset(o, 0, sizeof(*o));
    o->mode = MODE_LIST;
    o->format = FMT_PAX;
    o->no_abs_paths = 1;
    for (c = 1; c < argc; c++) {
        if (strcmp(argv[c], "--no-absolute-paths") == 0) {
            o->no_abs_paths = 1;
            memmove(&argv[c], &argv[c + 1], sizeof(char *) * (size_t)(argc - c));
            argc--;
            c--;
        }
    }
    opterr = 0;
    while ((c = getopt(argc, argv, "rwf:x:s:Lnp:vk")) != -1) {
        switch (c) {
        case 'r': o->mode = (o->mode == MODE_WRITE) ? MODE_COPY : MODE_READ; break;
        case 'w': o->mode = (o->mode == MODE_READ) ? MODE_COPY : MODE_WRITE; break;
        case 'f': o->archive = optarg; break;
        case 'x':
            if (!strcmp(optarg, "pax")) o->format = FMT_PAX;
            else if (!strcmp(optarg, "ustar") || !strcmp(optarg, "tar")) o->format = FMT_USTAR;
            else if (!strcmp(optarg, "cpio") || !strcmp(optarg, "newc")) o->format = FMT_CPIO_NEWC;
            else usage();
            break;
        case 's': parse_subst(o, optarg); break;
        case 'L': o->follow_links = 1; break;
        case 'n': o->no_overwrite = 1; break;
        case 'p': parse_preserve(o, optarg); break;
        case 'v': o->verbose = 1; break;
        case 'k': o->no_overwrite = 1; break;
        default: usage();
        }
    }
    o->first_nonopt = optind;
    if (o->mode == MODE_COPY) {
        if (optind >= argc) usage();
        o->copy_dest = argv[argc - 1];
    }
}

static void write_all(FILE *f, const void *buf, size_t n) {
    if (fwrite(buf, 1, n, f) != n) die("write archive");
}

static int read_full(FILE *f, void *buf, size_t n) {
    return fread(buf, 1, n, f) == n;
}

static unsigned tar_checksum(const struct tar_hdr *h) {
    const unsigned char *p = (const unsigned char *)h;
    unsigned s = 0;
    size_t i;
    for (i = 0; i < sizeof(*h); i++) s += p[i];
    return s;
}

static void tar_set_octal(char *dst, size_t n, unsigned long long v) {
    char tmp[64];
    size_t len;
    snprintf(tmp, sizeof(tmp), "%0*llo", (int)(n - 1), v);
    len = strlen(tmp);
    if (len > n - 1) {
        memcpy(dst, tmp + (len - (n - 1)), n);
    } else {
        memcpy(dst, tmp, len + 1);
    }
}

static void pax_append_record(char *dst, size_t dsz, const char *key, const char *val) {
    char payload[PATH_MAX + 64];
    char record[PATH_MAX + 96];
    int len, digits;

    snprintf(payload, sizeof(payload), "%s=%s", key, val);
    len = (int)strlen(payload) + 3;
    do {
        int t;
        digits = 1;
        t = len;
        while (t >= 10) { digits++; t /= 10; }
        len = (int)strlen(payload) + digits + 2;
    } while (0);

    snprintf(record, sizeof(record), "%d %s\n", len, payload);
    strncat(dst, record, dsz - strlen(dst) - 1);
}

static void write_tar_entry(FILE *f, const char *store_name, const struct stat *st, const char *linktarget, const char *disk_path, enum fmt_kind fmt) {
    struct tar_hdr h;
    char zero[TAR_BLOCK] = {0};
    char data[TAR_BLOCK];
    FILE *in;
    size_t r;
    long long rem;
    struct pax_kv kv;

    memset(&kv, 0, sizeof(kv));
    if (strlen(store_name) > 99) { kv.has_path = 1; snprintf(kv.path, sizeof(kv.path), "%s", store_name); }
    if (linktarget && strlen(linktarget) > 99) { kv.has_linkpath = 1; snprintf(kv.linkpath, sizeof(kv.linkpath), "%s", linktarget); }
    if (st->st_size > 077777777777LL) { kv.has_size = 1; kv.size = st->st_size; }
    if ((kv.has_path || kv.has_linkpath || kv.has_uid || kv.has_gid || kv.has_size) && fmt == FMT_PAX) {
        char ext[2048] = {0};
        char line[64];
        size_t ext_len = 0;
        if (kv.has_path) pax_append_record(ext, sizeof(ext), "path", kv.path);
        if (kv.has_linkpath) pax_append_record(ext, sizeof(ext), "linkpath", kv.linkpath);
        if (kv.has_uid) { snprintf(line, sizeof(line), "%ld", kv.uid); pax_append_record(ext, sizeof(ext), "uid", line); }
        if (kv.has_gid) { snprintf(line, sizeof(line), "%ld", kv.gid); pax_append_record(ext, sizeof(ext), "gid", line); }
        if (kv.has_size) { snprintf(line, sizeof(line), "%lld", kv.size); pax_append_record(ext, sizeof(ext), "size", line); }
        ext_len = strlen(ext);
        memset(&h, 0, sizeof(h));
        snprintf(h.name, sizeof(h.name), "%s", "PaxHeader");
        tar_set_octal(h.mode, sizeof(h.mode), 0644);
        tar_set_octal(h.uid, sizeof(h.uid), 0);
        tar_set_octal(h.gid, sizeof(h.gid), 0);
        tar_set_octal(h.size, sizeof(h.size), ext_len);
        tar_set_octal(h.mtime, sizeof(h.mtime), (unsigned long long)time(NULL));
        memset(h.chksum, ' ', sizeof(h.chksum));
        h.typeflag = 'x';
        memcpy(h.magic, "ustar", 5);
        memcpy(h.version, "00", 2);
        snprintf(h.chksum, sizeof(h.chksum), "%06o", tar_checksum(&h));
        write_all(f, &h, sizeof(h));
        write_all(f, ext, ext_len);
        if (ext_len % TAR_BLOCK) write_all(f, zero, TAR_BLOCK - (ext_len % TAR_BLOCK));
    }

    memset(&h, 0, sizeof(h));
    snprintf(h.name, sizeof(h.name), "%s", store_name);
    tar_set_octal(h.mode, sizeof(h.mode), st->st_mode & 07777);
    tar_set_octal(h.uid, sizeof(h.uid), st->st_uid);
    tar_set_octal(h.gid, sizeof(h.gid), st->st_gid);
    tar_set_octal(h.size, sizeof(h.size), S_ISREG(st->st_mode) ? (unsigned long long)st->st_size : 0);
    tar_set_octal(h.mtime, sizeof(h.mtime), (unsigned long long)st->st_mtime);
    memset(h.chksum, ' ', sizeof(h.chksum));
    h.typeflag = S_ISDIR(st->st_mode) ? '5' : (S_ISLNK(st->st_mode) ? '2' : '0');
    if (linktarget) snprintf(h.linkname, sizeof(h.linkname), "%s", linktarget);
    memcpy(h.magic, "ustar", 5);
    memcpy(h.version, "00", 2);
    snprintf(h.chksum, sizeof(h.chksum), "%06o", tar_checksum(&h));
    write_all(f, &h, sizeof(h));

    if (S_ISREG(st->st_mode)) {
        in = fopen(disk_path, "rb");
        if (!in) die("open input file");
        rem = st->st_size;
        while (rem > 0) {
            r = fread(data, 1, rem > TAR_BLOCK ? TAR_BLOCK : (size_t)rem, in);
            if (!r) die("read input file");
            write_all(f, data, r);
            rem -= (long long)r;
        }
        fclose(in);
        if (st->st_size % TAR_BLOCK) write_all(f, zero, TAR_BLOCK - (st->st_size % TAR_BLOCK));
    }
}

static void write_cpio_newc_entry(FILE *f, const char *store_name, const struct stat *st, const char *linktarget, const char *disk_path) {
    char hdr[111];
    FILE *in;
    char buf[4096];
    size_t r;
    unsigned namesz = (unsigned)strlen(store_name) + 1;
    unsigned filesz = S_ISREG(st->st_mode) ? (unsigned)st->st_size : (S_ISLNK(st->st_mode) ? (unsigned)strlen(linktarget) : 0);
    snprintf(hdr, sizeof(hdr),
             "070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X",
             1U, (unsigned)st->st_mode, (unsigned)st->st_uid, (unsigned)st->st_gid, (unsigned)st->st_nlink,
             (unsigned)st->st_mtime, filesz, 0U, 0U, 0U, 0U, namesz, 0U);
    write_all(f, hdr, 110);
    write_all(f, store_name, namesz);
    while ((110 + namesz) % 4) { fputc('\0', f); namesz++; }
    if (S_ISREG(st->st_mode)) {
        in = fopen(disk_path, "rb"); if (!in) die("open input file");
        while ((r = fread(buf, 1, sizeof(buf), in)) > 0) write_all(f, buf, r);
        fclose(in);
    } else if (S_ISLNK(st->st_mode)) {
        write_all(f, linktarget, strlen(linktarget));
    }
    while (filesz % 4) { fputc('\0', f); filesz++; }
}

static void walk_write(FILE *f, const struct opts *o, const char *path, const char *store_name) {
    struct stat st;
    DIR *d;
    struct dirent *de;
    char child_disk[PATH_MAX], child_store[PATH_MAX], lbuf[PATH_MAX];
    const char *linktarget = NULL;

    if ((o->follow_links ? stat(path, &st) : lstat(path, &st)) != 0) {
        fprintf(stderr, "pax: %s: %s\n", path, strerror(errno));
        return;
    }
    snprintf(child_store, sizeof(child_store), "%s", store_name);
    apply_subst(o, child_store, sizeof(child_store));

    if (S_ISLNK(st.st_mode) && !o->follow_links) {
        ssize_t n = readlink(path, lbuf, sizeof(lbuf)-1);
        if (n >= 0) { lbuf[n] = '\0'; linktarget = lbuf; }
    }

    if (o->format == FMT_CPIO_NEWC) write_cpio_newc_entry(f, child_store, &st, linktarget, path);
    else write_tar_entry(f, child_store, &st, linktarget, path, o->format);

    if (o->verbose) printf("%s\n", child_store);

    if (!S_ISDIR(st.st_mode)) return;
    d = opendir(path); if (!d) return;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        snprintf(child_disk, sizeof(child_disk), "%s/%s", path, de->d_name);
        snprintf(child_store, sizeof(child_store), "%s/%s", store_name, de->d_name);
        walk_write(f, o, child_disk, child_store);
    }
    closedir(d);
}

static void cmd_write(int argc, char **argv, const struct opts *o) {
    FILE *f = o->archive ? fopen(o->archive, "wb") : stdout;
    int i;
    if (!f) die("open archive for write");
    for (i = o->first_nonopt; i < argc; i++) walk_write(f, o, argv[i], argv[i]);
    if (o->format != FMT_CPIO_NEWC) {
        char z[TAR_BLOCK] = {0};
        write_all(f, z, TAR_BLOCK);
        write_all(f, z, TAR_BLOCK);
    } else {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_mode = 0100644;
        write_cpio_newc_entry(f, "TRAILER!!!", &st, NULL, NULL);
    }
    if (o->archive) fclose(f);
}

static int read_octal(const char *s, size_t n) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%.*s", (int)n, s);
    return (int)strtol(tmp, NULL, 8);
}

static void parse_pax_ext(struct pax_kv *kv, const char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        int len = atoi(buf + off);
        const char *sp = strchr(buf + off, ' ');
        const char *eq;
        if (len <= 0 || !sp || (size_t)len > n - off) break;
        eq = strchr(sp + 1, '=');
        if (eq) {
            size_t vlen = (size_t)len - (size_t)(eq - (buf + off)) - 2;
            if (!strncmp(sp + 1, "path", 4)) { snprintf(kv->path, sizeof(kv->path), "%.*s", (int)vlen, eq + 1); kv->has_path = 1; }
            else if (!strncmp(sp + 1, "linkpath", 8)) { snprintf(kv->linkpath, sizeof(kv->linkpath), "%.*s", (int)vlen, eq + 1); kv->has_linkpath = 1; }
            else if (!strncmp(sp + 1, "uid", 3)) { kv->uid = strtol(eq + 1, NULL, 10); kv->has_uid = 1; }
            else if (!strncmp(sp + 1, "gid", 3)) { kv->gid = strtol(eq + 1, NULL, 10); kv->has_gid = 1; }
            else if (!strncmp(sp + 1, "size", 4)) { kv->size = strtol(eq + 1, NULL, 10); kv->has_size = 1; }
            else if (!strncmp(sp + 1, "mtime", 5)) { kv->mtime = strtol(eq + 1, NULL, 10); kv->has_mtime = 1; }
        }
        off += (size_t)len;
    }
}

static void restore_attrs(const struct opts *o, const char *path, mode_t mode, uid_t uid, gid_t gid, time_t mtime) {
    if (o->preserve_mode) chmod(path, mode & 07777);
    if (o->preserve_owner) chown(path, uid, gid);
    (void)mtime;
}

static void extract_tar(FILE *f, const struct opts *o) {
    struct tar_hdr h;
    struct pax_kv kv;
    char path[PATH_MAX], raw[PATH_MAX], *data;
    size_t sz;
    FILE *out;
    int zeros = 0;
    memset(&kv, 0, sizeof(kv));

    while (read_full(f, &h, sizeof(h))) {
        if (h.name[0] == '\0') {
            zeros++;
            if (zeros >= 2) break;
            continue;
        }
        zeros = 0;
        sz = (size_t)read_octal(h.size, sizeof(h.size));
        if (h.typeflag == 'x') {
            data = malloc(sz + 1); if (!data) die("malloc");
            if (!read_full(f, data, sz)) die("read pax ext");
            data[sz] = '\0'; parse_pax_ext(&kv, data, sz); free(data);
            if (sz % TAR_BLOCK) fseek(f, (long)(TAR_BLOCK - (sz % TAR_BLOCK)), SEEK_CUR);
            continue;
        }
        if (kv.has_path) {
            snprintf(raw, sizeof(raw), "%s", kv.path);
        } else if (h.prefix[0] != '\0') {
            snprintf(raw, sizeof(raw), "%.*s/%.*s", (int)sizeof(h.prefix), h.prefix, (int)sizeof(h.name), h.name);
        } else {
            snprintf(raw, sizeof(raw), "%.*s", (int)sizeof(h.name), h.name);
        }
        apply_subst(o, raw, sizeof(raw));
        sanitize_path(o, raw, path, sizeof(path));
        if (!path[0]) { if (sz) fseek(f, (long)((sz + 511) & ~511), SEEK_CUR); memset(&kv,0,sizeof(kv)); continue; }

        if (o->list_only) {
            printf("%s\n", path);
        } else if (h.typeflag == '5') {
            mkdir(path, 0755);
        } else if (h.typeflag == '2') {
            const char *ln = kv.has_linkpath ? kv.linkpath : h.linkname;
            ensure_parent_dirs(path);
            symlink(ln, path);
        } else {
            if (o->no_overwrite && access(path, F_OK) == 0) {
                fseek(f, (long)((sz + 511) & ~511), SEEK_CUR);
                memset(&kv, 0, sizeof(kv));
                continue;
            }
            ensure_parent_dirs(path);
            out = fopen(path, "wb");
            if (!out) die("create output file");
            while (sz > 0) {
                char blk[TAR_BLOCK];
                size_t r = sz > TAR_BLOCK ? TAR_BLOCK : sz;
                if (!read_full(f, blk, TAR_BLOCK)) die("read tar data");
                fwrite(blk, 1, r, out);
                sz -= r;
            }
            fclose(out);
            restore_attrs(o, path, read_octal(h.mode, sizeof(h.mode)), kv.has_uid ? kv.uid : read_octal(h.uid, sizeof(h.uid)), kv.has_gid ? kv.gid : read_octal(h.gid, sizeof(h.gid)), 0);
            memset(&kv, 0, sizeof(kv));
            continue;
        }
        if (sz) fseek(f, (long)((sz + 511) & ~511), SEEK_CUR);
        restore_attrs(o, path, read_octal(h.mode, sizeof(h.mode)), kv.has_uid ? kv.uid : read_octal(h.uid, sizeof(h.uid)), kv.has_gid ? kv.gid : read_octal(h.gid, sizeof(h.gid)), 0);
        memset(&kv, 0, sizeof(kv));
    }
}

static unsigned read_hex_u32(const char *s, size_t n) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%.*s", (int)n, s);
    return (unsigned)strtol(tmp, NULL, 16);
}

static void extract_cpio_newc(FILE *f, const struct opts *o) {
    char hdr[110];
    while (read_full(f, hdr, 110)) {
        unsigned mode, filesize, namesize;
        char name[PATH_MAX], path[PATH_MAX];
        FILE *out;
        if (memcmp(hdr, "070701", 6) != 0 && memcmp(hdr, "070702", 6) != 0) break;
        mode = read_hex_u32(hdr + 14, 8);
        filesize = read_hex_u32(hdr + 54, 8);
        namesize = read_hex_u32(hdr + 94, 8);
        if (namesize >= sizeof(name)) die("cpio long name unsupported");
        if (!read_full(f, name, namesize)) die("read cpio name");
        if (!strcmp(name, "TRAILER!!!")) break;
        while ((110 + namesize) % 4) { fgetc(f); namesize++; }
        apply_subst(o, name, sizeof(name));
        sanitize_path(o, name, path, sizeof(path));
        if (!path[0]) { fseek(f, (long)((filesize + 3) & ~3), SEEK_CUR); continue; }
        if (o->list_only) printf("%s\n", path);
        else if ((mode & 0170000) == 0040000) mkdir(path, 0755);
        else if ((mode & 0170000) == 0120000) {
            char lbuf[PATH_MAX];
            if (filesize >= sizeof(lbuf)) die("symlink too long");
            read_full(f, lbuf, filesize); lbuf[filesize] = '\0';
            ensure_parent_dirs(path); symlink(lbuf, path);
        } else {
            unsigned left = filesize;
            char buf[4096];
            if (o->no_overwrite && access(path, F_OK) == 0) {
                fseek(f, (long)((filesize + 3) & ~3), SEEK_CUR);
                continue;
            }
            ensure_parent_dirs(path);
            out = fopen(path, "wb"); if (!out) die("create output");
            while (left) {
                size_t r = left > sizeof(buf) ? sizeof(buf) : left;
                if (!read_full(f, buf, r)) die("read cpio data");
                fwrite(buf, 1, r, out);
                left -= r;
            }
            fclose(out);
        }
        while (filesize % 4) { fgetc(f); filesize++; }
    }
}

static void cmd_read(const struct opts *o) {
    unsigned char probe[6];
    FILE *f = o->archive ? fopen(o->archive, "rb") : stdin;
    if (!f) die("open archive for read");
    if (fread(probe, 1, sizeof(probe), f) != sizeof(probe)) die("read archive");
    fseek(f, 0, SEEK_SET);
    if (!memcmp(probe, "070701", 6) || !memcmp(probe, "070702", 6)) extract_cpio_newc(f, o);
    else extract_tar(f, o);
    if (o->archive) fclose(f);
}

static int copy_file_data(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    FILE *out;
    char buf[4096];
    size_t r;
    if (!in) return -1;
    ensure_parent_dirs(dst);
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, r, out);
    fclose(in); fclose(out); return 0;
}

static void cmd_copy(int argc, char **argv, const struct opts *o) {
    int i;
    char dst[PATH_MAX];
    struct stat st;
    for (i = o->first_nonopt; i < argc - 1; i++) {
        const char *src = argv[i];
        const char *base = strrchr(src, '/');
        base = base ? base + 1 : src;
        snprintf(dst, sizeof(dst), "%s/%s", o->copy_dest, base);
        if (lstat(src, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) mkdir(dst, 0755);
        else if (S_ISREG(st.st_mode)) copy_file_data(src, dst);
    }
}

int main(int argc, char **argv) {
    struct opts o;
    parse_opts(argc, argv, &o);
    if (o.mode == MODE_LIST) {
        o.list_only = 1;
        cmd_read(&o);
    } else if (o.mode == MODE_READ) {
        cmd_read(&o);
    } else if (o.mode == MODE_WRITE) {
        cmd_write(argc, argv, &o);
    } else {
        cmd_copy(argc, argv, &o);
    }
    return 0;
}
