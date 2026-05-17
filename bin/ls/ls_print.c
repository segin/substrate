#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include "ls_colors.h"
#include "ls_print.h"

#ifdef NATIVE_BUILD
#include <sys/ioctl.h>
#endif
/* sys/xattr.h is provided by both substrate libc (target) and glibc/
 * Apple libc on common hosts.  Older FreeBSD-y hosts may lack it;
 * the listxattr/getxattr call sites are no-ops if listxattr returns
 * <= 0, so a host without xattr just silently won't show them.  */
#if __has_include(<sys/xattr.h>)
#include <sys/xattr.h>
#endif

#ifndef major
#define major(x) (((x) >> 8) & 0xff)
#endif

#ifndef minor
#define minor(x) ((x) & 0xff)
#endif

typedef struct {
    uid_t id;
    char name[64];
    bool valid;
} uid_cache_entry_t;

typedef struct {
    gid_t id;
    char name[64];
    bool valid;
} gid_cache_entry_t;

typedef struct {
    int inode_w;
    int blocks_w;
    int links_w;
    int owner_w;
    int group_w;
    int size_w;
    bool has_device;
} long_widths_t;

static uid_cache_entry_t uid_cache[32];
static gid_cache_entry_t gid_cache[32];

static int digits_u64(unsigned long long value) {
    int digits = 1;

    while (value >= 10ULL) {
        value /= 10ULL;
        digits++;
    }

    return digits;
}

static int utf8_char_width(const char *s, size_t len, size_t *consumed) {
    mbstate_t st;
    wchar_t wc;
    size_t rc;
    int width;

    memset(&st, 0, sizeof(st));
    rc = mbrtowc(&wc, s, len, &st);
    if (rc == (size_t)-1 || rc == (size_t)-2) {
        *consumed = 1;
        return (unsigned char)s[0] < 0x20 ? 2 : 1;
    }
    if (rc == 0) {
        *consumed = 1;
        return 0;
    }

    *consumed = rc;
    width = wcwidth(wc);
    return width < 0 ? 1 : width;
}

static int display_width(const char *text) {
    int width = 0;
    size_t i = 0;
    size_t len;

    if (text == NULL) {
        return 0;
    }

    len = strlen(text);
    while (i < len) {
        size_t consumed = 1;
        width += utf8_char_width(text + i, len - i, &consumed);
        i += consumed;
    }

    return width;
}

static char *append_char(char *dst, size_t *len, size_t *cap, char ch) {
    char *grown;

    if (*len + 2 > *cap) {
        size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
        grown = (char *)realloc(dst, new_cap);
        if (grown == NULL) {
            free(dst);
            return NULL;
        }
        dst = grown;
        *cap = new_cap;
    }

    dst[*len] = ch;
    (*len)++;
    dst[*len] = '\0';
    return dst;
}

static char *append_text(char *dst, size_t *len, size_t *cap, const char *text) {
    size_t need;
    char *grown;

    need = strlen(text);
    if (*len + need + 1 > *cap) {
        size_t new_cap = (*cap == 0) ? 64 : *cap;
        while (*len + need + 1 > new_cap) {
            new_cap *= 2;
        }
        grown = (char *)realloc(dst, new_cap);
        if (grown == NULL) {
            free(dst);
            return NULL;
        }
        dst = grown;
        *cap = new_cap;
    }

    memcpy(dst + *len, text, need + 1);
    *len += need;
    return dst;
}

static char *quote_literal(const char *name, bool hide_control_chars) {
    size_t i;
    size_t len = 0;
    size_t cap = 0;
    char *out = NULL;

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (hide_control_chars && (iscntrl(ch) || !isprint(ch))) {
            out = append_char(out, &len, &cap, '?');
        } else {
            out = append_char(out, &len, &cap, (char)ch);
        }
        if (out == NULL) {
            return NULL;
        }
    }

    if (out == NULL) {
        out = strdup("");
    }
    return out;
}

static bool shell_safe_char(unsigned char ch) {
    return isalnum(ch) || ch == '_' || ch == '.' || ch == '/' || ch == '-' || ch == '+';
}

static char *quote_escape(const char *name) {
    size_t i;
    size_t len = 0;
    size_t cap = 0;
    char *out = NULL;

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)name[i];
        switch (ch) {
            case '\n':
                out = append_text(out, &len, &cap, "\\n");
                break;
            case '\t':
                out = append_text(out, &len, &cap, "\\t");
                break;
            case '\r':
                out = append_text(out, &len, &cap, "\\r");
                break;
            case '\\':
            case ' ':
            case '"':
            case '\'':
                out = append_char(out, &len, &cap, '\\');
                if (out != NULL) {
                    out = append_char(out, &len, &cap, (char)ch);
                }
                break;
            default:
                if (iscntrl(ch)) {
                    char buf[5];
                    snprintf(buf, sizeof(buf), "\\%03o", ch);
                    out = append_text(out, &len, &cap, buf);
                } else {
                    out = append_char(out, &len, &cap, (char)ch);
                }
                break;
        }
        if (out == NULL) {
            return NULL;
        }
    }

    if (out == NULL) {
        out = strdup("");
    }
    return out;
}

static char *quote_c_style(const char *name) {
    size_t i;
    size_t len = 0;
    size_t cap = 0;
    char *out = NULL;

    out = append_char(out, &len, &cap, '"');
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)name[i];
        switch (ch) {
            case '\n':
                out = append_text(out, &len, &cap, "\\n");
                break;
            case '\t':
                out = append_text(out, &len, &cap, "\\t");
                break;
            case '\r':
                out = append_text(out, &len, &cap, "\\r");
                break;
            case '\\':
            case '"':
                out = append_char(out, &len, &cap, '\\');
                if (out != NULL) {
                    out = append_char(out, &len, &cap, (char)ch);
                }
                break;
            default:
                if (iscntrl(ch)) {
                    char buf[5];
                    snprintf(buf, sizeof(buf), "\\%03o", ch);
                    out = append_text(out, &len, &cap, buf);
                } else {
                    out = append_char(out, &len, &cap, (char)ch);
                }
                break;
        }
        if (out == NULL) {
            return NULL;
        }
    }

    return append_char(out, &len, &cap, '"');
}

static char *quote_shell(const char *name, bool always_quote) {
    size_t i;
    size_t len = 0;
    size_t cap = 0;
    char *out = NULL;
    bool needs_quotes = always_quote;

    for (i = 0; name[i] != '\0'; i++) {
        if (!shell_safe_char((unsigned char)name[i])) {
            needs_quotes = true;
            break;
        }
    }

    if (!needs_quotes) {
        return strdup(name);
    }

    out = append_char(out, &len, &cap, '\'');
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; name[i] != '\0'; i++) {
        if (name[i] == '\'') {
            out = append_text(out, &len, &cap, "'\\''");
        } else {
            out = append_char(out, &len, &cap, name[i]);
        }
        if (out == NULL) {
            return NULL;
        }
    }

    return append_char(out, &len, &cap, '\'');
}

static char file_type_char(mode_t mode) {
    if (S_ISDIR(mode)) return 'd';
    if (S_ISLNK(mode)) return 'l';
    if (S_ISCHR(mode)) return 'c';
    if (S_ISBLK(mode)) return 'b';
    if (S_ISFIFO(mode)) return 'p';
    if (S_ISSOCK(mode)) return 's';
    return '-';
}

static char classify_suffix(const file_info_t *file, const ls_config_t *config) {
    mode_t mode = file->st.st_mode;

    if (!config->classify && !config->slash_dirs && !config->file_type) {
        return '\0';
    }

    if (S_ISDIR(mode)) {
        return '/';
    }
    if (config->slash_dirs) {
        return '\0';
    }
    if (S_ISLNK(mode)) {
        return '@';
    }
    if (S_ISFIFO(mode)) {
        return '|';
    }
    if (S_ISSOCK(mode)) {
        return '=';
    }
    if (S_ISREG(mode) && !config->file_type &&
        (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) {
        return '*';
    }
    return '\0';
}

static bool use_color(const ls_config_t *config) {
    if (config->color == LS_COLOR_ALWAYS) {
        return true;
    }
    return false;
}

static const char *lookup_user(uid_t uid) {
    size_t i;
    struct passwd *pw;

    for (i = 0; i < sizeof(uid_cache) / sizeof(uid_cache[0]); i++) {
        if (uid_cache[i].valid && uid_cache[i].id == uid) {
            return uid_cache[i].name;
        }
    }

    pw = getpwuid(uid);
    if (pw == NULL || pw->pw_name == NULL) {
        return NULL;
    }

    i = (size_t)uid % (sizeof(uid_cache) / sizeof(uid_cache[0]));
    uid_cache[i].id = uid;
    uid_cache[i].valid = true;
    snprintf(uid_cache[i].name, sizeof(uid_cache[i].name), "%s", pw->pw_name);
    return uid_cache[i].name;
}

static const char *lookup_group(gid_t gid) {
    size_t i;
    struct group *gr;

    for (i = 0; i < sizeof(gid_cache) / sizeof(gid_cache[0]); i++) {
        if (gid_cache[i].valid && gid_cache[i].id == gid) {
            return gid_cache[i].name;
        }
    }

    gr = getgrgid(gid);
    if (gr == NULL || gr->gr_name == NULL) {
        return NULL;
    }

    i = (size_t)gid % (sizeof(gid_cache) / sizeof(gid_cache[0]));
    gid_cache[i].id = gid;
    gid_cache[i].valid = true;
    snprintf(gid_cache[i].name, sizeof(gid_cache[i].name), "%s", gr->gr_name);
    return gid_cache[i].name;
}

static void format_mode(const file_info_t *file, char out[12]) {
    mode_t mode = file->st.st_mode;

    out[0] = file_type_char(mode);
    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_ISUID) ? ((mode & S_IXUSR) ? 's' : 'S') : ((mode & S_IXUSR) ? 'x' : '-');
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_ISGID) ? ((mode & S_IXGRP) ? 's' : 'S') : ((mode & S_IXGRP) ? 'x' : '-');
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_ISVTX) ? ((mode & S_IXOTH) ? 't' : 'T') : ((mode & S_IXOTH) ? 'x' : '-');
    out[10] = ' ';
    out[11] = '\0';

    if (file->full_path != NULL && !file->display_as_symlink && listxattr(file->full_path, NULL, 0) > 0) {
        out[10] = '@';
    }
}

static void format_size(const ls_config_t *config, off_t size, char *buf, size_t bufsz) {
    static const char *units_1024[] = {"B", "K", "M", "G", "T", "P"};
    static const char *units_1000[] = {"B", "K", "M", "G", "T", "P"};
    double value;
    int idx = 0;
    int base = config->si_units ? 1000 : 1024;
    const char *const *units = config->si_units ? units_1000 : units_1024;

    if (!config->human_readable) {
        snprintf(buf, bufsz, "%lld", (long long)size);
        return;
    }

    value = (double)size;
    while (value >= (double)base && idx < 5) {
        value /= (double)base;
        idx++;
    }

    if (idx == 0 || value >= 10.0) {
        snprintf(buf, bufsz, "%.0f%s", value, units[idx]);
    } else {
        snprintf(buf, bufsz, "%.1f%s", value, units[idx]);
    }
}

static time_t select_time_value(const file_info_t *file, const ls_config_t *config) {
    switch (config->time_type) {
        case TIME_ATIME:
            return file->st.st_atime;
        case TIME_CTIME:
            return file->st.st_ctime;
        case TIME_MTIME:
        default:
            return file->st.st_mtime;
    }
}

static void format_time_value(const file_info_t *file, const ls_config_t *config, char *buf, size_t bufsz) {
    time_t when = select_time_value(file, config);
    struct tm tmv;
    time_t now;
    double delta;
    const char *fmt = "%b %e %H:%M";

    if (localtime_r(&when, &tmv) == NULL) {
        snprintf(buf, bufsz, "?");
        return;
    }

    switch (config->time_style) {
        case TIME_STYLE_FULL_ISO:
            strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S %z", &tmv);
            return;
        case TIME_STYLE_LONG_ISO:
            strftime(buf, bufsz, "%Y-%m-%d %H:%M", &tmv);
            return;
        case TIME_STYLE_ISO:
            strftime(buf, bufsz, "%Y-%m-%d", &tmv);
            return;
        case TIME_STYLE_CUSTOM:
            strftime(buf, bufsz, config->time_style_format ? config->time_style_format : "%c", &tmv);
            return;
        case TIME_STYLE_LOCALE:
        default:
            break;
    }

    time(&now);
    delta = difftime(now, when);
    if (delta < 0) {
        delta = -delta;
    }
    if (delta >= 60.0 * 60.0 * 24.0 * 30.0 * 6.0) {
        fmt = "%b %e  %Y";
    }
    strftime(buf, bufsz, fmt, &tmv);
}

static char *quote_name(const ls_config_t *config, const char *name) {
    if (config->quote_names) {
        return quote_c_style(name);
    }

    switch (config->quoting_style) {
        case LS_QUOTE_SHELL:
            return quote_shell(name, false);
        case LS_QUOTE_SHELL_ALWAYS:
            return quote_shell(name, true);
        case LS_QUOTE_C:
            return quote_c_style(name);
        case LS_QUOTE_ESCAPE:
            return quote_escape(name);
        case LS_QUOTE_LITERAL:
        default:
            return quote_literal(name, config->hide_control_chars);
    }
}

static char *compose_name(const file_info_t *file, const ls_config_t *config) {
    char *quoted;
    char *out;
    char suffix;
    size_t need;

    quoted = quote_name(config, file->name);
    if (quoted == NULL) {
        return NULL;
    }

    suffix = classify_suffix(file, config);
    if (config->dereference && (file->dangling_link || file->symlink_loop)) {
        need = strlen(quoted) + 3 + (suffix ? 1 : 0);
        out = (char *)malloc(need);
        if (out != NULL) {
            if (suffix != '\0') {
                snprintf(out, need, "[%s]%c", quoted, suffix);
            } else {
                snprintf(out, need, "[%s]", quoted);
            }
        }
    } else {
        need = strlen(quoted) + 1 + (suffix ? 1 : 0);
        out = (char *)malloc(need);
        if (out != NULL) {
            if (suffix != '\0') {
                snprintf(out, need, "%s%c", quoted, suffix);
            } else {
                snprintf(out, need, "%s", quoted);
            }
        }
    }

    free(quoted);
    return out;
}

static void print_colored_name(const file_info_t *file, const ls_config_t *config, const char *name) {
    if (use_color(config)) {
        const char *color = ls_colors_get(file->name, file->st.st_mode);
        if (color != NULL && *color != '\0') {
            fputs(color, stdout);
            fputs(name, stdout);
            fputs(ls_colors_reset(), stdout);
            return;
        }
    }
    fputs(name, stdout);
}

static blkcnt_t file_blocks(const file_info_t *file, const ls_config_t *config) {
    if (config->kibibytes) {
        return (blkcnt_t)((file->st.st_size + 1023) / 1024);
    }
    if (config->block_size > 0) {
        return (blkcnt_t)((file->st.st_size + config->block_size - 1) / config->block_size);
    }
    if (file->st.st_blocks > 0) {
        return file->st.st_blocks;
    }
    return (blkcnt_t)((file->st.st_size + 511) / 512);
}

static void compute_long_widths(file_info_t *files, size_t count, const ls_config_t *config, long_widths_t *w) {
    size_t i;

    memset(w, 0, sizeof(*w));
    w->inode_w = config->inode ? 1 : 0;
    w->blocks_w = config->show_blocks ? 1 : 0;
    w->links_w = 1;
    w->owner_w = 1;
    w->group_w = 1;
    w->size_w = 1;

    for (i = 0; i < count; i++) {
        char sizebuf[64];
        const char *owner;
        const char *group;

        if (config->inode) {
            int width = digits_u64((unsigned long long)files[i].st.st_ino);
            if (width > w->inode_w) {
                w->inode_w = width;
            }
        }
        if (config->show_blocks) {
            int width = digits_u64((unsigned long long)file_blocks(&files[i], config));
            if (width > w->blocks_w) {
                w->blocks_w = width;
            }
        }

        {
            int width = digits_u64((unsigned long long)files[i].st.st_nlink);
            if (width > w->links_w) {
                w->links_w = width;
            }
        }

        owner = config->numeric_ids ? NULL : lookup_user(files[i].st.st_uid);
        group = config->numeric_ids ? NULL : lookup_group(files[i].st.st_gid);

        if (!config->no_owner) {
            int width = owner ? (int)strlen(owner) : digits_u64((unsigned long long)files[i].st.st_uid);
            if (width > w->owner_w) {
                w->owner_w = width;
            }
        }
        if (!config->no_group) {
            int width = group ? (int)strlen(group) : digits_u64((unsigned long long)files[i].st.st_gid);
            if (width > w->group_w) {
                w->group_w = width;
            }
        }

        if (S_ISCHR(files[i].st.st_mode) || S_ISBLK(files[i].st.st_mode)) {
            char devbuf[64];
            snprintf(devbuf, sizeof(devbuf), "%u,%u",
                     (unsigned)major(files[i].st.st_rdev), (unsigned)minor(files[i].st.st_rdev));
            w->has_device = true;
            if ((int)strlen(devbuf) > w->size_w) {
                w->size_w = (int)strlen(devbuf);
            }
        } else {
            format_size(config, files[i].st.st_size, sizebuf, sizeof(sizebuf));
            if ((int)strlen(sizebuf) > w->size_w) {
                w->size_w = (int)strlen(sizebuf);
            }
        }
    }
}

static void maybe_print_total(file_info_t *files, size_t count, const ls_config_t *config,
                              bool show_total_blocks) {
    blkcnt_t total = 0;
    size_t i;

    if (!show_total_blocks || (!config->long_fmt && !config->show_blocks)) {
        return;
    }

    for (i = 0; i < count; i++) {
        total += file_blocks(&files[i], config);
    }

    printf("total %lld\n", (long long)total);
}

static void print_long_entry(const file_info_t *file, const ls_config_t *config, const long_widths_t *w) {
    char mode[12];
    char timebuf[64];
    char sizebuf[64];
    char *name;
    const char *owner;
    const char *group;

    name = compose_name(file, config);
    if (name == NULL) {
        return;
    }

    format_mode(file, mode);
    format_time_value(file, config, timebuf, sizeof(timebuf));

    if (config->inode) {
        printf("%*llu ", w->inode_w, (unsigned long long)file->st.st_ino);
    }
    if (config->show_blocks) {
        printf("%*lld ", w->blocks_w, (long long)file_blocks(file, config));
    }

    printf("%s %*llu", mode, w->links_w, (unsigned long long)file->st.st_nlink);

    owner = config->numeric_ids ? NULL : lookup_user(file->st.st_uid);
    group = config->numeric_ids ? NULL : lookup_group(file->st.st_gid);

    if (!config->no_owner) {
        if (owner != NULL) {
            printf(" %-*s", w->owner_w, owner);
        } else {
            printf(" %*llu", w->owner_w, (unsigned long long)file->st.st_uid);
        }
    }
    if (!config->no_group) {
        if (group != NULL) {
            printf(" %-*s", w->group_w, group);
        } else {
            printf(" %*llu", w->group_w, (unsigned long long)file->st.st_gid);
        }
    }

    if (S_ISCHR(file->st.st_mode) || S_ISBLK(file->st.st_mode)) {
        snprintf(sizebuf, sizeof(sizebuf), "%u,%u",
                 (unsigned)major(file->st.st_rdev), (unsigned)minor(file->st.st_rdev));
    } else {
        format_size(config, file->st.st_size, sizebuf, sizeof(sizebuf));
    }

    printf(" %*s %s ", w->size_w, sizebuf, timebuf);
    print_colored_name(file, config, name);

    if (file->display_as_symlink && file->link_target != NULL) {
        printf(" -> %s", file->link_target);
    }

    if (config->list_xattr_names && file->full_path != NULL) {
        ssize_t sz = listxattr(file->full_path, NULL, 0);
        if (sz > 0) {
            char *buf = (char *)malloc((size_t)sz);
            if (buf != NULL) {
                ssize_t got = listxattr(file->full_path, buf, (size_t)sz);
                if (got > 0) {
                    ssize_t off = 0;
                    while (off < got) {
                        printf("\n\t%s", buf + off);
                        off += (ssize_t)strlen(buf + off) + 1;
                    }
                }
                free(buf);
            }
        }
    }

    putchar('\n');
    free(name);
}

static void print_single_column(file_info_t *files, size_t count, const ls_config_t *config) {
    size_t i;

    for (i = 0; i < count; i++) {
        char *name = compose_name(&files[i], config);
        if (name == NULL) {
            continue;
        }

        if (config->inode) {
            printf("%llu ", (unsigned long long)files[i].st.st_ino);
        }
        if (config->show_blocks) {
            printf("%lld ", (long long)file_blocks(&files[i], config));
        }

        print_colored_name(&files[i], config, name);
        putchar('\n');
        free(name);
    }
}

static void print_comma_separated(file_info_t *files, size_t count, const ls_config_t *config) {
    size_t i;

    for (i = 0; i < count; i++) {
        char *name = compose_name(&files[i], config);
        if (name == NULL) {
            continue;
        }

        if (i > 0) {
            fputs(", ", stdout);
        }
        print_colored_name(&files[i], config, name);
        free(name);
    }
    putchar('\n');
}

static int determine_term_width(const ls_config_t *config) {
    if (config->term_width > 0) {
        return config->term_width;
    }
#ifdef NATIVE_BUILD
    {
        struct winsize ws;
        if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
            return ws.ws_col;
        }
    }
#endif
    return 80;
}

static void print_multi_column(file_info_t *files, size_t count, const ls_config_t *config) {
    char **names;
    int *widths;
    int term_width;
    size_t cols;
    size_t rows;
    size_t i;

    if (count == 0) {
        return;
    }

    names = (char **)calloc(count, sizeof(*names));
    widths = (int *)calloc(count, sizeof(*widths));
    if (names == NULL || widths == NULL) {
        free(names);
        free(widths);
        print_single_column(files, count, config);
        return;
    }

    term_width = determine_term_width(config);

    for (i = 0; i < count; i++) {
        names[i] = compose_name(&files[i], config);
        if (names[i] == NULL) {
            names[i] = strdup("?");
        }
        widths[i] = display_width(names[i]);
    }

    cols = count;
    while (cols > 1) {
        size_t candidate_rows = (count + cols - 1) / cols;
        size_t c;
        int used = 0;

        for (c = 0; c < cols; c++) {
            int col_w = 0;
            size_t r;
            for (r = 0; r < candidate_rows; r++) {
                size_t idx = config->by_lines ? (r * cols + c) : (c * candidate_rows + r);
                if (idx < count && widths[idx] > col_w) {
                    col_w = widths[idx];
                }
            }
            if (col_w == 0) {
                continue;
            }
            used += col_w;
            if (c + 1 < cols) {
                used += 2;
            }
        }

        if (used <= term_width) {
            break;
        }
        cols--;
    }

    rows = (count + cols - 1) / cols;
    for (i = 0; i < rows; i++) {
        size_t c;
        for (c = 0; c < cols; c++) {
            size_t idx = config->by_lines ? (i * cols + c) : (c * rows + i);
            int col_w = 0;
            size_t r;

            if (idx >= count) {
                continue;
            }

            for (r = 0; r < rows; r++) {
                size_t probe = config->by_lines ? (r * cols + c) : (c * rows + r);
                if (probe < count && widths[probe] > col_w) {
                    col_w = widths[probe];
                }
            }

            print_colored_name(&files[idx], config, names[idx]);
            if (c + 1 < cols) {
                int pad = col_w - widths[idx] + 2;
                while (pad-- > 0) {
                    putchar(' ');
                }
            }
        }
        putchar('\n');
    }

    for (i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
    free(widths);
}

void ls_print_list(const char *label, file_info_t *files, size_t count,
                   const ls_config_t *config, bool show_total_blocks) {
    long_widths_t widths;
    size_t i;

    (void)label;

    if (count == 0) {
        return;
    }

    maybe_print_total(files, count, config, show_total_blocks);

    if (config->long_fmt) {
        compute_long_widths(files, count, config, &widths);
        for (i = 0; i < count; i++) {
            print_long_entry(&files[i], config, &widths);
        }
        return;
    }

    if (config->comma_sep) {
        print_comma_separated(files, count, config);
        return;
    }

    if (config->multi_column && !config->one_per_line) {
        print_multi_column(files, count, config);
        return;
    }

    print_single_column(files, count, config);
}
