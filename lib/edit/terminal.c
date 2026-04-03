#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "el.h"

/* ------------------------------------------------------------------ */
/* Termcap database parser                                            */
/* ------------------------------------------------------------------ */

#define TC_PATH     "/etc/termcap"
#define TC_BUFSZ    4096

/*
 * Decode termcap escape notation in-place:
 *   \E  → ESC (0x1B)
 *   \n  → newline, \r → CR, \t → tab, \b → BS, \f → FF
 *   \^  → literal ^
 *   \\  → literal backslash
 *   ^X  → Ctrl-X
 *   \nnn → octal
 */
static void tc_decode(char *s) {
    char *r = s, *w = s;

    while (*r) {
        if (*r == '\\') {
            r++;
            switch (*r) {
            case 'E': case 'e': *w++ = '\033'; r++; break;
            case 'n': *w++ = '\n'; r++; break;
            case 'r': *w++ = '\r'; r++; break;
            case 't': *w++ = '\t'; r++; break;
            case 'b': *w++ = '\b'; r++; break;
            case 'f': *w++ = '\f'; r++; break;
            case '^': *w++ = '^';  r++; break;
            case '\\': *w++ = '\\'; r++; break;
            case '0': case '1': case '2': case '3':
            case '4': case '5': case '6': case '7': {
                unsigned v = 0;
                int i;
                for (i = 0; i < 3 && *r >= '0' && *r <= '7'; i++, r++)
                    v = (v << 3) | (unsigned)(*r - '0');
                *w++ = (char)v;
                break;
            }
            default:
                if (*r) *w++ = *r++;
                break;
            }
        } else if (*r == '^') {
            r++;
            if (*r) {
                *w++ = (char)(*r & 0x1F);
                r++;
            }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

/*
 * Find a termcap entry for 'name' in the file at 'path'.
 * Returns a malloc'd buffer with the full (continuation-joined) entry,
 * or NULL if not found.  Handles one level of tc= inheritance.
 */
static char *tc_find_entry(const char *path, const char *name, int depth) {
    FILE *fp;
    char line[1024];
    char *entry = NULL;
    size_t entry_len = 0, entry_cap = 0;
    int found = 0;

    if (!name || !name[0] || depth > 4) return NULL;

    fp = fopen(path, "r");
    if (!fp) return NULL;

    while (fgets(line, (int)sizeof(line), fp)) {
        size_t len = strlen(line);

        /* Strip trailing newline */
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        /* Skip comments and blank lines when not in continuation */
        if (!found && (line[0] == '#' || line[0] == '\0'))
            continue;

        if (!found) {
            /* Check if this line's name list contains our terminal */
            char *colon = strchr(line, ':');
            if (!colon) continue;

            /* Temporarily terminate at first colon to scan names */
            *colon = '\0';
            {
                char *tok = line;
                char *sep;
                int match = 0;
                while ((sep = strchr(tok, '|')) != NULL) {
                    *sep = '\0';
                    if (strcmp(tok, name) == 0) match = 1;
                    *sep = '|';
                    if (match) break;
                    tok = sep + 1;
                }
                if (!match && strcmp(tok, name) == 0) match = 1;
                *colon = ':';
                if (!match) continue;
            }
            found = 1;
            /* Start collecting from the first ':' */
            {
                char *start = colon;
                size_t slen = strlen(start);
                entry = malloc(slen + 1);
                if (!entry) { fclose(fp); return NULL; }
                memcpy(entry, start, slen + 1);
                entry_len = slen;
                entry_cap = slen + 1;
            }
        } else {
            /* Continuation line (starts with whitespace) */
            if (line[0] != '\t' && line[0] != ' ') break;

            /* Skip leading whitespace */
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;

            size_t plen = strlen(p);
            if (entry_len + plen + 1 > entry_cap) {
                size_t nc = (entry_cap + plen + 1) * 2;
                char *nb = realloc(entry, nc);
                if (!nb) { free(entry); fclose(fp); return NULL; }
                entry = nb;
                entry_cap = nc;
            }
            memcpy(entry + entry_len, p, plen + 1);
            entry_len += plen;
        }
    }
    fclose(fp);

    if (!entry) return NULL;

    /* Check for backslash continuation at end — already handled by line reading */

    /* Handle tc= inheritance (one level) */
    {
        char *tc = strstr(entry, ":tc=");
        if (tc) {
            char ref[128];
            char *eq = tc + 4;
            char *end = strchr(eq, ':');
            size_t rlen = end ? (size_t)(end - eq) : strlen(eq);
            if (rlen > 0 && rlen < sizeof(ref)) {
                memcpy(ref, eq, rlen);
                ref[rlen] = '\0';

                /* Remove the tc= field from entry */
                if (end) {
                    memmove(tc, end, strlen(end) + 1);
                    entry_len = strlen(entry);
                } else {
                    *tc = '\0';
                    entry_len = (size_t)(tc - entry);
                }

                /* Look up parent entry */
                char *parent = tc_find_entry(path, ref, depth + 1);
                if (parent) {
                    size_t plen = strlen(parent);
                    if (entry_len + plen + 1 > entry_cap) {
                        size_t nc = entry_len + plen + 2;
                        char *nb = realloc(entry, nc);
                        if (!nb) { free(parent); return entry; }
                        entry = nb;
                        entry_cap = nc;
                    }
                    memcpy(entry + entry_len, parent, plen + 1);
                    entry_len += plen;
                    free(parent);
                }
            }
        }
    }

    return entry;
}

/*
 * Extract a string capability from a termcap entry.
 * Returns a malloc'd decoded string, or NULL if not found.
 */
static char *tc_get_str(const char *entry, const char *cap) {
    char needle[8];
    const char *p;
    const char *end;
    size_t len;
    char *result;

    snprintf(needle, sizeof(needle), ":%s=", cap);
    p = strstr(entry, needle);
    if (!p) return NULL;

    p += strlen(needle);
    end = strchr(p, ':');
    len = end ? (size_t)(end - p) : strlen(p);

    result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, p, len);
    result[len] = '\0';
    tc_decode(result);
    return result;
}

/*
 * Extract a numeric capability from a termcap entry.
 * Returns the value, or -1 if not found.
 */
static int tc_get_num(const char *entry, const char *cap) {
    char needle[8];
    const char *p;

    snprintf(needle, sizeof(needle), ":%s#", cap);
    p = strstr(entry, needle);
    if (!p) return -1;

    return atoi(p + strlen(needle));
}

/*
 * ANSI/VT100 fallback capabilities.
 */
static const struct {
    const char *name;
    const char *value;
} ansi_fallbacks[] = {
    { "le", "\033[D" },
    { "nd", "\033[C" },
    { "up", "\033[A" },
    { "do", "\033[B" },  /* "do" cap, stored as do_cap */
    { "ho", "\033[H" },
    { "cl", "\033[H\033[J" },
    { "ce", "\033[K" },
    { "cd", "\033[J" },
    { "cm", "\033[%i%d;%dH" },
    { "dc", "\033[P" },
    { "ic", "\033[@" },
    { "al", "\033[L" },
    { "dl", "\033[M" },
    { "md", "\033[1m" },
    { "me", "\033[0m" },
    { "so", "\033[7m" },
    { "se", "\033[27m" },
    { "sr", "\033M" },
    { "sf", "\n" },
    { NULL, NULL }
};

static char *ansi_fallback(const char *cap) {
    int i;
    for (i = 0; ansi_fallbacks[i].name; i++) {
        if (strcmp(ansi_fallbacks[i].name, cap) == 0)
            return strdup(ansi_fallbacks[i].value);
    }
    return NULL;
}

/* Helper: get cap from termcap entry or fall back to ANSI */
static char *tc_get_or_fallback(const char *entry, const char *cap) {
    char *val = entry ? tc_get_str(entry, cap) : NULL;
    if (!val) val = ansi_fallback(cap);
    return val;
}

void terminal_init_caps(EditLine *el) {
    const char *term;
    char *entry;
    struct termcap_caps *caps;

    if (!el) return;
    caps = &el->term.caps;
    memset(caps, 0, sizeof(*caps));

    term = getenv("TERM");
    entry = term ? tc_find_entry(TC_PATH, term, 0) : NULL;

    /* String capabilities */
    caps->cm     = tc_get_or_fallback(entry, "cm");
    caps->le     = tc_get_or_fallback(entry, "le");
    caps->nd     = tc_get_or_fallback(entry, "nd");
    caps->up     = tc_get_or_fallback(entry, "up");
    caps->do_cap = tc_get_or_fallback(entry, "do");
    caps->ho     = tc_get_or_fallback(entry, "ho");
    caps->cl     = tc_get_or_fallback(entry, "cl");
    caps->ce     = tc_get_or_fallback(entry, "ce");
    caps->cd     = tc_get_or_fallback(entry, "cd");
    caps->ic     = tc_get_or_fallback(entry, "ic");
    caps->dc     = tc_get_or_fallback(entry, "dc");
    caps->al     = tc_get_or_fallback(entry, "al");
    caps->dl     = tc_get_or_fallback(entry, "dl");
    caps->md     = tc_get_or_fallback(entry, "md");
    caps->me     = tc_get_or_fallback(entry, "me");
    caps->so     = tc_get_or_fallback(entry, "so");
    caps->se     = tc_get_or_fallback(entry, "se");
    caps->sr     = tc_get_or_fallback(entry, "sr");
    caps->sf     = tc_get_or_fallback(entry, "sf");

    /* Numeric capabilities */
    caps->co = entry ? tc_get_num(entry, "co") : -1;
    caps->li = entry ? tc_get_num(entry, "li") : -1;

    caps->loaded = 1;
    free(entry);
}

void terminal_free_caps(EditLine *el) {
    struct termcap_caps *c;
    if (!el) return;
    c = &el->term.caps;

    free(c->cm);   free(c->le);  free(c->nd);     free(c->up);
    free(c->do_cap); free(c->ho); free(c->cl);     free(c->ce);
    free(c->cd);   free(c->ic);  free(c->dc);     free(c->al);
    free(c->dl);   free(c->md);  free(c->me);     free(c->so);
    free(c->se);   free(c->sr);  free(c->sf);
    memset(c, 0, sizeof(*c));
}

/* ------------------------------------------------------------------ */
/* Raw mode                                                           */
/* ------------------------------------------------------------------ */

int terminal_set_raw(EditLine *el) {
    if (el->term.is_raw) return 0;

    if (tcgetattr(fileno(el->fin), &el->term.orig) == -1)
        return -1;

    el->term.raw = el->term.orig;
    /* Basic raw mode: disable echo, canonical mode, signals, and extended processing */
    el->term.raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    el->term.raw.c_oflag &= ~(OPOST);
    el->term.raw.c_cflag |= (CS8);
    el->term.raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    el->term.raw.c_cc[VMIN] = 1;
    el->term.raw.c_cc[VTIME] = 0;

    if (tcsetattr(fileno(el->fin), TCSAFLUSH, &el->term.raw) == -1)
        return -1;

    el->term.is_raw = 1;
    return 0;
}

int terminal_set_orig(EditLine *el) {
    if (!el->term.is_raw) return 0;

    if (tcsetattr(fileno(el->fin), TCSAFLUSH, &el->term.orig) == -1)
        return -1;

    el->term.is_raw = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Terminal dimensions with cache invalidation                        */
/* ------------------------------------------------------------------ */

void terminal_get_size(EditLine *el) {
    struct winsize ws;
    const char *env;

    if (!el) return;

    /* Try ioctl first */
    if (el->fout &&
        ioctl(fileno(el->fout), TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0 && ws.ws_row > 0) {
        el->term.cols = ws.ws_col;
        el->term.rows = ws.ws_row;
        el->term.dims_valid = 1;
        return;
    }

    /* Fall back to environment variables */
    env = getenv("COLUMNS");
    if (env) {
        int v = atoi(env);
        if (v > 0) el->term.cols = v;
    }
    env = getenv("LINES");
    if (env) {
        int v = atoi(env);
        if (v > 0) el->term.rows = v;
    }

    /* Default 80x24 if nothing else worked */
    if (el->term.cols <= 0) el->term.cols = 80;
    if (el->term.rows <= 0) el->term.rows = 24;

    el->term.dims_valid = 1;
}

/* ------------------------------------------------------------------ */
/* Output buffering                                                   */
/* ------------------------------------------------------------------ */

void terminal_write(EditLine *el, const char *data, size_t len) {
    if (!el || !data || len == 0) return;

    while (len > 0) {
        size_t avail = EL_OUTBUF_SIZE - el->term.outbuf_len;
        size_t chunk = (len < avail) ? len : avail;

        memcpy(el->term.outbuf + el->term.outbuf_len, data, chunk);
        el->term.outbuf_len += chunk;
        data += chunk;
        len -= chunk;

        if (el->term.outbuf_len >= EL_OUTBUF_SIZE)
            terminal_flush(el);
    }
}

void terminal_puts(EditLine *el, const char *s) {
    if (s) terminal_write(el, s, strlen(s));
}

void terminal_putc(EditLine *el, char c) {
    terminal_write(el, &c, 1);
}

void terminal_printf(EditLine *el, const char *fmt, ...) {
    va_list ap;
    char buf[512];
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0)
        terminal_write(el, buf, (size_t)len);
}

void terminal_flush(EditLine *el) {
    if (!el || el->term.outbuf_len == 0) return;

    if (el->fout) {
        int fd = fileno(el->fout);
        const char *p = el->term.outbuf;
        size_t remaining = el->term.outbuf_len;

        while (remaining > 0) {
            ssize_t n = write(fd, p, remaining);
            if (n <= 0) break;
            p += n;
            remaining -= (size_t)n;
        }
    }
    el->term.outbuf_len = 0;
}
