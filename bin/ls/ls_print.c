#if defined(NATIVE_BUILD) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include <ctype.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#if defined(__has_include)
#if __has_include(<sys/xattr.h>)
#define LS_HAVE_XATTR 1
#include <sys/xattr.h>
#endif
#if __has_include(<sys/sysmacros.h>)
#include <sys/sysmacros.h>
#endif
#endif

#include "ls_colors.h"
#include "ls_print.h"

#ifndef LS_HAVE_XATTR
#define LS_HAVE_XATTR 0
#endif

#ifndef major
#define major(dev) ((unsigned int)(((dev) >> 8) & 0xffu))
#endif
#ifndef minor
#define minor(dev) ((unsigned int)((dev) & 0xffu))
#endif
#ifndef S_ISUID
#define S_ISUID 04000
#endif
#ifndef S_ISGID
#define S_ISGID 02000
#endif
#ifndef S_ISVTX
#define S_ISVTX 01000
#endif

#define UID_CACHE_MAX 128
#define GID_CACHE_MAX 128

typedef struct {
    uid_t uid;
    bool used;
    char name[32];
} uid_cache_entry_t;

typedef struct {
    gid_t gid;
    bool used;
    char name[32];
} gid_cache_entry_t;

typedef struct {
    char *text;
    size_t width;
} ls_cell_t;

static uid_cache_entry_t g_uid_cache[UID_CACHE_MAX];
static gid_cache_entry_t g_gid_cache[GID_CACHE_MAX];
static size_t g_uid_next;
static size_t g_gid_next;

static int should_colorize(const ls_config_t *config) {
    if (config->color == LS_COLOR_ALWAYS) {
        return 1;
    }
    if (config->color == LS_COLOR_AUTO) {
        return isatty(STDOUT_FILENO) != 0;
    }
    return 0;
}

static int get_term_width(const ls_config_t *config) {
    struct winsize ws;
    const char *cols;

    if (config->term_width > 0) {
        return config->term_width;
    }

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }

    cols = getenv("COLUMNS");
    if (cols != NULL && *cols != '\0') {
        int n = atoi(cols);
        if (n > 0) {
            return n;
        }
    }

    return 80;
}

static size_t count_digits_u64(uint64_t v) {
    size_t n = 1;
    while (v >= 10) {
        v /= 10;
        n++;
    }
    return n;
}

static long long block_unit(const ls_config_t *config) {
    if (config->block_size > 0) {
        return config->block_size;
    }
    if (config->kibibytes) {
        return 1024;
    }
    return 1024;
}

static long long blocks_to_units(blkcnt_t blocks, long long unit) {
    long long bytes = (long long)blocks * 512LL;
    if (unit <= 0) {
        unit = 1;
    }
    if (bytes <= 0) {
        return 0;
    }
    return (bytes + unit - 1) / unit;
}

static void append_str(char **buf, size_t *len, size_t *cap, const char *text, size_t tlen) {
    size_t need;
    char *nbuf;

    if (tlen == 0) {
        return;
    }

    need = *len + tlen + 1;
    if (need > *cap) {
        size_t ncap = (*cap == 0) ? 64 : *cap;
        while (ncap < need) {
            ncap *= 2;
        }
        nbuf = (char *)realloc(*buf, ncap);
        if (nbuf == NULL) {
            return;
        }
        *buf = nbuf;
        *cap = ncap;
    }

    memcpy(*buf + *len, text, tlen);
    *len += tlen;
    (*buf)[*len] = '\0';
}

static void append_char(char **buf, size_t *len, size_t *cap, char c) {
    append_str(buf, len, cap, &c, 1);
}

static int nongraphic(unsigned char c) {
    if (c == '\t' || c == '\n') {
        return 0;
    }
    return !isprint(c);
}

static char *sanitize_literal(const char *name, bool hide_controls) {
    size_t i;
    size_t n = strlen(name);
    char *out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        out[i] = (hide_controls && nongraphic(c)) ? '?' : (char)c;
    }
    out[n] = '\0';
    return out;
}

static char *quote_c_style(const char *name, bool hide_controls) {
    size_t i;
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;

    append_char(&buf, &len, &cap, '"');
    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        if (hide_controls && nongraphic(c)) {
            append_char(&buf, &len, &cap, '?');
            continue;
        }
        switch (c) {
            case '\\': append_str(&buf, &len, &cap, "\\\\", 2); break;
            case '"': append_str(&buf, &len, &cap, "\\\"", 2); break;
            case '\n': append_str(&buf, &len, &cap, "\\n", 2); break;
            case '\t': append_str(&buf, &len, &cap, "\\t", 2); break;
            case '\r': append_str(&buf, &len, &cap, "\\r", 2); break;
            default:
                if (!isprint(c)) {
                    char tmp[5];
                    snprintf(tmp, sizeof(tmp), "\\%03o", c);
                    append_str(&buf, &len, &cap, tmp, strlen(tmp));
                } else {
                    append_char(&buf, &len, &cap, (char)c);
                }
                break;
        }
    }
    append_char(&buf, &len, &cap, '"');

    if (buf == NULL) {
        return strdup("\"\"");
    }
    return buf;
}

static int needs_shell_quotes(const char *name) {
    size_t i;
    if (*name == '\0') {
        return 1;
    }
    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isspace(c) || strchr("'\"`$&;()[]{}<>|*?!#~", c) != NULL) {
            return 1;
        }
    }
    return 0;
}

static char *quote_shell(const char *name, bool always_quote, bool hide_controls) {
    char *lit;
    size_t i;
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;

    lit = sanitize_literal(name, hide_controls);
    if (lit == NULL) {
        return NULL;
    }

    if (!always_quote && !needs_shell_quotes(lit)) {
        return lit;
    }

    append_char(&buf, &len, &cap, '\'');
    for (i = 0; lit[i] != '\0'; i++) {
        if (lit[i] == '\'') {
            append_str(&buf, &len, &cap, "'\\''", 4);
        } else {
            append_char(&buf, &len, &cap, lit[i]);
        }
    }
    append_char(&buf, &len, &cap, '\'');
    free(lit);

    if (buf == NULL) {
        return strdup("''");
    }
    return buf;
}

static char *quote_escape(const char *name, bool hide_controls) {
    size_t i;
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        if (hide_controls && nongraphic(c)) {
            append_char(&buf, &len, &cap, '?');
            continue;
        }

        if (c == '\n') {
            append_str(&buf, &len, &cap, "\\n", 2);
        } else if (c == '\t') {
            append_str(&buf, &len, &cap, "\\t", 2);
        } else if (c == '\r') {
            append_str(&buf, &len, &cap, "\\r", 2);
        } else if (isspace(c) || c == '\\' || c == '\'' || c == '"') {
            append_char(&buf, &len, &cap, '\\');
            append_char(&buf, &len, &cap, (char)c);
        } else if (!isprint(c)) {
            char tmp[5];
            snprintf(tmp, sizeof(tmp), "\\%03o", c);
            append_str(&buf, &len, &cap, tmp, strlen(tmp));
        } else {
            append_char(&buf, &len, &cap, (char)c);
        }
    }

    if (buf == NULL) {
        return strdup("");
    }
    return buf;
}

static size_t display_width_utf8(const char *s) {
    size_t width = 0;
    const unsigned char *p = (const unsigned char *)s;

    while (*p != '\0') {
        uint32_t cp;
        size_t step = 1;
        int w = 1;

        if ((p[0] & 0x80u) == 0) {
            cp = p[0];
            step = 1;
        } else if ((p[0] & 0xE0u) == 0xC0u &&
                   p[1] != '\0' &&
                   (p[1] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(p[0] & 0x1Fu) << 6) |
                 (uint32_t)(p[1] & 0x3Fu);
            step = 2;
        } else if ((p[0] & 0xF0u) == 0xE0u &&
                   p[1] != '\0' &&
                   p[2] != '\0' &&
                   (p[1] & 0xC0u) == 0x80u &&
                   (p[2] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(p[0] & 0x0Fu) << 12) |
                 ((uint32_t)(p[1] & 0x3Fu) << 6) |
                 (uint32_t)(p[2] & 0x3Fu);
            step = 3;
        } else if ((p[0] & 0xF8u) == 0xF0u &&
                   p[1] != '\0' &&
                   p[2] != '\0' &&
                   p[3] != '\0' &&
                   (p[1] & 0xC0u) == 0x80u &&
                   (p[2] & 0xC0u) == 0x80u &&
                   (p[3] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(p[0] & 0x07u) << 18) |
                 ((uint32_t)(p[1] & 0x3Fu) << 12) |
                 ((uint32_t)(p[2] & 0x3Fu) << 6) |
                 (uint32_t)(p[3] & 0x3Fu);
            step = 4;
        } else {
            cp = p[0];
            step = 1;
        }

        if (cp <= 0x10FFFFu) {
            int ww = wcwidth((wchar_t)cp);
            if (ww >= 0) {
                w = ww;
            } else if ((cp >= 0x0300u && cp <= 0x036Fu) ||
                       (cp >= 0x1AB0u && cp <= 0x1AFFu) ||
                       (cp >= 0x1DC0u && cp <= 0x1DFFu) ||
                       (cp >= 0x20D0u && cp <= 0x20FFu) ||
                       (cp >= 0xFE20u && cp <= 0xFE2Fu)) {
                w = 0;
            } else if ((cp >= 0x1100u && cp <= 0x115Fu) ||
                       (cp >= 0x2E80u && cp <= 0xA4CFu) ||
                       (cp >= 0xAC00u && cp <= 0xD7A3u) ||
                       (cp >= 0xF900u && cp <= 0xFAFFu) ||
                       (cp >= 0xFE10u && cp <= 0xFE19u) ||
                       (cp >= 0xFE30u && cp <= 0xFE6Fu) ||
                       (cp >= 0xFF00u && cp <= 0xFF60u) ||
                       (cp >= 0xFFE0u && cp <= 0xFFE6u)) {
                w = 2;
            } else {
                w = 1;
            }
        }

        if (w > 0) {
            width += (size_t)w;
        }
        p += step;
    }

    return width;
}

static char indicator_char(const file_info_t *f, const ls_config_t *config) {
    mode_t mode = f->st.st_mode;

    if (config->classify) {
        if (S_ISDIR(mode)) return '/';
        if (S_ISLNK(mode)) return '@';
        if (S_ISFIFO(mode)) return '|';
        if (S_ISSOCK(mode)) return '=';
        if ((mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) return '*';
    } else if (config->file_type) {
        if (S_ISDIR(mode)) return '/';
        if (S_ISLNK(mode)) return '@';
        if (S_ISFIFO(mode)) return '|';
        if (S_ISSOCK(mode)) return '=';
    } else if (config->slash_dirs && S_ISDIR(mode)) {
        return '/';
    }

    return '\0';
}

static char *format_name_plain(const file_info_t *f, const ls_config_t *config, size_t *width_out) {
    char *base = NULL;
    char *tmp = NULL;
    size_t base_len;
    char ind;

    if (config->quote_names) {
        base = quote_c_style(f->name, config->hide_control_chars);
    } else if (config->literal || config->quoting_style == LS_QUOTE_LITERAL) {
        base = sanitize_literal(f->name, config->hide_control_chars);
    } else if (config->quoting_style == LS_QUOTE_C) {
        base = quote_c_style(f->name, config->hide_control_chars);
    } else if (config->quoting_style == LS_QUOTE_ESCAPE) {
        base = quote_escape(f->name, config->hide_control_chars);
    } else if (config->quoting_style == LS_QUOTE_SHELL) {
        base = quote_shell(f->name, false, config->hide_control_chars);
    } else {
        base = quote_shell(f->name, true, config->hide_control_chars);
    }

    if (base == NULL) {
        base = strdup("?");
        if (base == NULL) {
            return NULL;
        }
    }

    ind = indicator_char(f, config);
    if (ind != '\0') {
        base_len = strlen(base);
        tmp = (char *)malloc(base_len + 2);
        if (tmp == NULL) {
            free(base);
            return NULL;
        }
        memcpy(tmp, base, base_len);
        tmp[base_len] = ind;
        tmp[base_len + 1] = '\0';
        free(base);
        base = tmp;
    }

    if (width_out != NULL) {
        *width_out = display_width_utf8(base);
    }
    return base;
}

static char *colorize_name(const file_info_t *f, const char *plain, const ls_config_t *config) {
    const char *color;
    const char *reset;
    size_t plen;
    size_t clen;
    size_t rlen;
    char *out;

    if (!should_colorize(config)) {
        return strdup(plain);
    }

    color = ls_colors_get(f->name, f->st.st_mode);
    if (color == NULL || *color == '\0') {
        return strdup(plain);
    }

    reset = ls_colors_reset();
    plen = strlen(plain);
    clen = strlen(color);
    rlen = strlen(reset);

    out = (char *)malloc(clen + plen + rlen + 1);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, color, clen);
    memcpy(out + clen, plain, plen);
    memcpy(out + clen + plen, reset, rlen);
    out[clen + plen + rlen] = '\0';

    return out;
}

static char *build_short_cell(const file_info_t *f, const ls_config_t *config,
                              size_t block_w, size_t inode_w, size_t *visible_width) {
    char prefix[128];
    char *plain;
    char *name;
    char *cell;
    size_t w = 0;
    int n = 0;

    if (config->show_blocks) {
        long long units = blocks_to_units(f->st.st_blocks, block_unit(config));
        n += snprintf(prefix + n, sizeof(prefix) - (size_t)n, "%*lld ", (int)block_w, units);
        w += block_w + 1;
    }
    if (config->inode) {
        n += snprintf(prefix + n, sizeof(prefix) - (size_t)n, "%*llu ", (int)inode_w,
                      (unsigned long long)f->st.st_ino);
        w += inode_w + 1;
    }

    plain = format_name_plain(f, config, &w);
    if (plain == NULL) {
        return NULL;
    }

    if (config->show_blocks || config->inode) {
        size_t prefix_len = strlen(prefix);
        size_t name_w = 0;
        char *cp;

        free(plain);
        plain = format_name_plain(f, config, &name_w);
        if (plain == NULL) {
            return NULL;
        }

        cp = colorize_name(f, plain, config);
        if (cp == NULL) {
            free(plain);
            return NULL;
        }

        cell = (char *)malloc(prefix_len + strlen(cp) + 1);
        if (cell == NULL) {
            free(plain);
            free(cp);
            return NULL;
        }
        memcpy(cell, prefix, prefix_len);
        strcpy(cell + prefix_len, cp);

        *visible_width = (prefix_len) + name_w;
        free(plain);
        free(cp);
        return cell;
    }

    name = colorize_name(f, plain, config);
    if (name == NULL) {
        free(plain);
        return NULL;
    }

    *visible_width = w;
    free(plain);
    return name;
}

static void lookup_user(uid_t uid, int numeric, char *buf, size_t bufsz) {
    size_t i;

    if (numeric) {
        snprintf(buf, bufsz, "%u", (unsigned)uid);
        return;
    }

    for (i = 0; i < UID_CACHE_MAX; i++) {
        if (g_uid_cache[i].used && g_uid_cache[i].uid == uid) {
            snprintf(buf, bufsz, "%s", g_uid_cache[i].name);
            return;
        }
    }

    {
        struct passwd *pw = getpwuid(uid);
        if (pw != NULL && pw->pw_name != NULL) {
            size_t slot = g_uid_next++ % UID_CACHE_MAX;
            g_uid_cache[slot].uid = uid;
            g_uid_cache[slot].used = true;
            snprintf(g_uid_cache[slot].name, sizeof(g_uid_cache[slot].name), "%s", pw->pw_name);
            snprintf(buf, bufsz, "%s", g_uid_cache[slot].name);
            return;
        }
    }

    snprintf(buf, bufsz, "%u", (unsigned)uid);
}

static void lookup_group(gid_t gid, int numeric, char *buf, size_t bufsz) {
    size_t i;

    if (numeric) {
        snprintf(buf, bufsz, "%u", (unsigned)gid);
        return;
    }

    for (i = 0; i < GID_CACHE_MAX; i++) {
        if (g_gid_cache[i].used && g_gid_cache[i].gid == gid) {
            snprintf(buf, bufsz, "%s", g_gid_cache[i].name);
            return;
        }
    }

    {
        struct group *gr = getgrgid(gid);
        if (gr != NULL && gr->gr_name != NULL) {
            size_t slot = g_gid_next++ % GID_CACHE_MAX;
            g_gid_cache[slot].gid = gid;
            g_gid_cache[slot].used = true;
            snprintf(g_gid_cache[slot].name, sizeof(g_gid_cache[slot].name), "%s", gr->gr_name);
            snprintf(buf, bufsz, "%s", g_gid_cache[slot].name);
            return;
        }
    }

    snprintf(buf, bufsz, "%u", (unsigned)gid);
}

static int is_recent_time(time_t t) {
    time_t now = time(NULL);
    const time_t six_months = (time_t)(365 / 2 * 24 * 60 * 60);
    if (t > now + 3600) {
        return 0;
    }
    return t >= now - six_months;
}

static void format_time(const file_info_t *f, const ls_config_t *config, char *buf, size_t bufsz) {
    time_t t;
    struct tm tmv;
    const char *fmt = "%b %e %H:%M";

    if (config->time_type == TIME_ATIME) {
        t = f->st.st_atime;
    } else if (config->time_type == TIME_CTIME) {
        t = f->st.st_ctime;
    } else {
        t = f->st.st_mtime;
    }

    {
        struct tm *ptm = localtime(&t);
        if (ptm == NULL) {
            snprintf(buf, bufsz, "????????????");
            return;
        }
        tmv = *ptm;
    }

    if (config->time_style == TIME_STYLE_FULL_ISO) {
        fmt = "%Y-%m-%d %H:%M:%S %z";
    } else if (config->time_style == TIME_STYLE_LONG_ISO) {
        fmt = "%Y-%m-%d %H:%M";
    } else if (config->time_style == TIME_STYLE_ISO) {
        fmt = is_recent_time(t) ? "%m-%d %H:%M" : "%Y-%m-%d";
    } else if (config->time_style == TIME_STYLE_CUSTOM && config->time_style_format != NULL) {
        fmt = config->time_style_format;
    } else {
        fmt = is_recent_time(t) ? "%b %e %H:%M" : "%b %e  %Y";
    }

    strftime(buf, bufsz, fmt, &tmv);
}

static void format_scaled_size(off_t size, const ls_config_t *config, char *buf, size_t bufsz) {
    if (config->human_readable) {
        static const char *units[] = {"B", "K", "M", "G", "T", "P"};
        int base = config->si_units ? 1000 : 1024;
        double d = (double)size;
        size_t idx = 0;
        while (d >= base && idx + 1 < (sizeof(units) / sizeof(units[0]))) {
            d /= base;
            idx++;
        }
        if (idx == 0) {
            snprintf(buf, bufsz, "%lld", (long long)size);
        } else {
            snprintf(buf, bufsz, "%.1f%s", d, units[idx]);
        }
        return;
    }

    if (config->block_size > 0) {
        long long unit = config->block_size;
        long long v = ((long long)size + unit - 1) / unit;
        snprintf(buf, bufsz, "%lld", v);
        return;
    }

    snprintf(buf, bufsz, "%lld", (long long)size);
}

static char detect_attr_indicator(const file_info_t *f) {
#if LS_HAVE_XATTR
    ssize_t n = listxattr(f->full_path, NULL, 0);
    if (n > 0) {
        char *buf = (char *)malloc((size_t)n + 1);
        if (buf != NULL) {
            ssize_t got = listxattr(f->full_path, buf, (size_t)n);
            if (got > 0) {
                bool has_acl = false;
                bool has_other = false;
                size_t i = 0;
                while (i < (size_t)got) {
                    const char *name = buf + i;
                    size_t l = strlen(name);
                    if (strcmp(name, "system.posix_acl_access") == 0 ||
                        strcmp(name, "system.posix_acl_default") == 0) {
                        has_acl = true;
                    } else {
                        has_other = true;
                    }
                    i += l + 1;
                }
                free(buf);
                if (has_acl) {
                    return '+';
                }
                if (has_other) {
                    return '@';
                }
                return ' ';
            }
            free(buf);
        }
    }
#else
    (void)f;
#endif
    return ' ';
}

static void print_xattr_names(const file_info_t *f) {
#if LS_HAVE_XATTR
    ssize_t n = listxattr(f->full_path, NULL, 0);
    if (n > 0) {
        char *buf = (char *)malloc((size_t)n + 1);
        if (buf != NULL) {
            ssize_t got = listxattr(f->full_path, buf, (size_t)n);
            if (got > 0) {
                size_t i = 0;
                int first = 1;
                printf("\t");
                while (i < (size_t)got) {
                    const char *name = buf + i;
                    size_t l = strlen(name);
                    if (l == 0) {
                        break;
                    }
                    if (!first) {
                        printf(", ");
                    }
                    printf("%s", name);
                    first = 0;
                    i += l + 1;
                }
                printf("\n");
            }
            free(buf);
        }
    }
#else
    (void)f;
#endif
}

static void format_mode_string(const file_info_t *f, char out[12]) {
    mode_t m = f->st.st_mode;

    out[0] = S_ISDIR(m)  ? 'd' :
             S_ISLNK(m)  ? 'l' :
             S_ISCHR(m)  ? 'c' :
             S_ISBLK(m)  ? 'b' :
             S_ISFIFO(m) ? 'p' :
             S_ISSOCK(m) ? 's' : '-';

    out[1] = (m & S_IRUSR) ? 'r' : '-';
    out[2] = (m & S_IWUSR) ? 'w' : '-';
    if (m & S_ISUID) {
        out[3] = (m & S_IXUSR) ? 's' : 'S';
    } else {
        out[3] = (m & S_IXUSR) ? 'x' : '-';
    }

    out[4] = (m & S_IRGRP) ? 'r' : '-';
    out[5] = (m & S_IWGRP) ? 'w' : '-';
    if (m & S_ISGID) {
        out[6] = (m & S_IXGRP) ? 's' : 'S';
    } else {
        out[6] = (m & S_IXGRP) ? 'x' : '-';
    }

    out[7] = (m & S_IROTH) ? 'r' : '-';
    out[8] = (m & S_IWOTH) ? 'w' : '-';
    if (m & S_ISVTX) {
        out[9] = (m & S_IXOTH) ? 't' : 'T';
    } else {
        out[9] = (m & S_IXOTH) ? 'x' : '-';
    }

    out[10] = detect_attr_indicator(f);
    out[11] = '\0';
}

static void print_long(const char *label, file_info_t *files, size_t count,
                       const ls_config_t *config, bool show_total_blocks) {
    size_t i;
    size_t nlink_w = 1;
    size_t owner_w = 1;
    size_t group_w = 1;
    size_t size_w = 1;
    size_t inode_w = 1;
    size_t blocks_w = 1;
    long long total = 0;

    (void)label;

    for (i = 0; i < count; i++) {
        char owner[64];
        char group[64];
        char sizebuf[64];

        if (files[i].stat_ok) {
            total += blocks_to_units(files[i].st.st_blocks, block_unit(config));
        }

        if (config->show_blocks) {
            long long b = blocks_to_units(files[i].st.st_blocks, block_unit(config));
            size_t d = count_digits_u64((uint64_t)(b < 0 ? 0 : b));
            if (d > blocks_w) blocks_w = d;
        }

        if (config->inode) {
            size_t d = count_digits_u64((uint64_t)files[i].st.st_ino);
            if (d > inode_w) inode_w = d;
        }

        {
            size_t d = count_digits_u64((uint64_t)files[i].st.st_nlink);
            if (d > nlink_w) nlink_w = d;
        }

        if (!config->no_owner) {
            lookup_user(files[i].st.st_uid, config->numeric_ids, owner, sizeof(owner));
            if (strlen(owner) > owner_w) owner_w = strlen(owner);
        }

        if (!config->no_group) {
            lookup_group(files[i].st.st_gid, config->numeric_ids, group, sizeof(group));
            if (strlen(group) > group_w) group_w = strlen(group);
        }

        if (S_ISCHR(files[i].st.st_mode) || S_ISBLK(files[i].st.st_mode)) {
            snprintf(sizebuf, sizeof(sizebuf), "%u,%u",
                     major(files[i].st.st_rdev), minor(files[i].st.st_rdev));
        } else {
            format_scaled_size(files[i].st.st_size, config, sizebuf, sizeof(sizebuf));
        }
        if (strlen(sizebuf) > size_w) size_w = strlen(sizebuf);
    }

    if (show_total_blocks) {
        printf("total %lld\n", total);
    }

    for (i = 0; i < count; i++) {
        char mode[12];
        char owner[64];
        char group[64];
        char sizebuf[64];
        char timebuf[128];
        char *plain;
        char *name;

        if (config->show_blocks) {
            long long b = blocks_to_units(files[i].st.st_blocks, block_unit(config));
            printf("%*lld ", (int)blocks_w, b);
        }
        if (config->inode) {
            printf("%*llu ", (int)inode_w, (unsigned long long)files[i].st.st_ino);
        }

        format_mode_string(&files[i], mode);
        printf("%s ", mode);
        printf("%*llu ", (int)nlink_w, (unsigned long long)files[i].st.st_nlink);

        if (!config->no_owner) {
            lookup_user(files[i].st.st_uid, config->numeric_ids, owner, sizeof(owner));
            printf("%-*s ", (int)owner_w, owner);
        }

        if (!config->no_group) {
            lookup_group(files[i].st.st_gid, config->numeric_ids, group, sizeof(group));
            printf("%-*s ", (int)group_w, group);
        }

        if (S_ISCHR(files[i].st.st_mode) || S_ISBLK(files[i].st.st_mode)) {
            snprintf(sizebuf, sizeof(sizebuf), "%u,%u",
                     major(files[i].st.st_rdev), minor(files[i].st.st_rdev));
        } else {
            format_scaled_size(files[i].st.st_size, config, sizebuf, sizeof(sizebuf));
        }
        printf("%*s ", (int)size_w, sizebuf);

        format_time(&files[i], config, timebuf, sizeof(timebuf));
        printf("%s ", timebuf);

        plain = format_name_plain(&files[i], config, NULL);
        if (plain == NULL) {
            plain = strdup("?");
        }
        name = colorize_name(&files[i], plain, config);
        if (name == NULL) {
            name = strdup(plain ? plain : "?");
        }

        printf("%s", name ? name : "?");

        if (files[i].display_as_symlink && files[i].link_target != NULL) {
            printf(" -> %s", files[i].link_target);
        }
        if (files[i].dangling_link) {
            printf(" [dangling]");
        }
        if (files[i].symlink_loop) {
            printf(" [loop]");
        }
        printf("\n");
        if (config->list_xattr_names) {
            print_xattr_names(&files[i]);
        }

        free(plain);
        free(name);
    }
}

static void print_short(file_info_t *files, size_t count, const ls_config_t *config, int mode) {
    size_t i;
    ls_cell_t *cells;
    size_t max_w = 0;
    size_t block_w = 1;
    size_t inode_w = 1;

    if (count == 0) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (config->show_blocks) {
            long long b = blocks_to_units(files[i].st.st_blocks, block_unit(config));
            size_t d = count_digits_u64((uint64_t)(b < 0 ? 0 : b));
            if (d > block_w) block_w = d;
        }
        if (config->inode) {
            size_t d = count_digits_u64((uint64_t)files[i].st.st_ino);
            if (d > inode_w) inode_w = d;
        }
    }

    cells = (ls_cell_t *)calloc(count, sizeof(ls_cell_t));
    if (cells == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        cells[i].text = build_short_cell(&files[i], config, block_w, inode_w, &cells[i].width);
        if (cells[i].text == NULL) {
            cells[i].text = strdup("?");
            cells[i].width = 1;
        }
        if (cells[i].width > max_w) {
            max_w = cells[i].width;
        }
    }

    if (mode == 2) {
        for (i = 0; i < count; i++) {
            printf("%s", cells[i].text);
            if (i + 1 < count) {
                printf(", ");
            }
        }
        printf("\n");
    } else if (mode == 1) {
        for (i = 0; i < count; i++) {
            printf("%s\n", cells[i].text);
        }
    } else {
        int term_width = get_term_width(config);
        size_t col_width;
        size_t cols;
        size_t rows;
        size_t r;
        size_t c;

        if (term_width < 20) {
            for (i = 0; i < count; i++) {
                printf("%s\n", cells[i].text);
            }
            goto done;
        }

        col_width = max_w + 2;
        if (col_width == 0) {
            col_width = 1;
        }

        cols = (size_t)term_width / col_width;
        if (cols == 0) {
            cols = 1;
        }
        rows = (count + cols - 1) / cols;

        for (r = 0; r < rows; r++) {
            for (c = 0; c < cols; c++) {
                size_t idx;
                size_t next_idx;
                size_t pad;

                if (mode == 4) {
                    idx = r * cols + c;
                    next_idx = r * cols + c + 1;
                } else {
                    idx = c * rows + r;
                    next_idx = (c + 1) * rows + r;
                }

                if (idx >= count) {
                    continue;
                }

                printf("%s", cells[idx].text);

                if (c + 1 < cols && next_idx < count) {
                    pad = col_width > cells[idx].width ? col_width - cells[idx].width : 1;
                    while (pad-- > 0) {
                        putchar(' ');
                    }
                }
            }
            putchar('\n');
        }
    }

done:
    for (i = 0; i < count; i++) {
        free(cells[i].text);
    }
    free(cells);
}

void ls_print_list(const char *label, file_info_t *files, size_t count,
                   const ls_config_t *config, bool show_total_blocks) {
    int mode;

    (void)label;
    if (config->long_fmt) {
        print_long(label, files, count, config, show_total_blocks);
        return;
    }

    if (config->comma_sep) {
        mode = 2;
    } else if (config->one_per_line) {
        mode = 1;
    } else if (config->by_lines) {
        mode = 4;
    } else if (config->multi_column) {
        mode = 3;
    } else {
        mode = isatty(STDOUT_FILENO) ? 3 : 1;
    }

    print_short(files, count, config, mode);
}
