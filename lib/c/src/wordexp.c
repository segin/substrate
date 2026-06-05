/*
 * wordexp.c — POSIX word expansion, layered over substrate's glob(3).
 *
 * wordexp() applies the shell's word expansions to a string, in order:
 *
 *   1. Tilde expansion        ~  -> $HOME,  ~user -> that user's home dir.
 *   2. Parameter expansion    $VAR and ${VAR} from the environment.
 *   3. Field splitting        unquoted runs of IFS (space/tab/newline) break
 *                             the input — and the result of an *unquoted*
 *                             parameter expansion — into separate words.
 *   4. Pathname expansion      each field is glob(3)'d; a field that matches
 *                             nothing is taken literally (shell default with
 *                             nullglob off, i.e. GLOB_NOCHECK).
 *   5. Quote removal           handled inline as the input is scanned.
 *
 * Single quotes quote everything literally; double quotes suppress field
 * splitting and pathname expansion but still allow $-expansion; a backslash
 * escapes the next character outside single quotes.
 *
 * Command substitution (`...` and $(...)) and arithmetic ($((...))) are NOT
 * executed: if encountered, wordexp() returns WRDE_CMDSUB — the same answer a
 * shell gives when the caller passes WRDE_NOCMD.  Unquoted shell metacharacters
 * ( | & ; < > ( ) { } ) yield WRDE_BADCHAR, and unbalanced quotes WRDE_SYNTAX.
 */

#include <wordexp.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

/* ---- a growable char buffer for the field being assembled ---- */
struct buf {
    char  *p;
    size_t len, cap;
};

static int buf_putc(struct buf *b, char c) {
    if (b->len + 1 >= b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 64;
        char *np = realloc(b->p, ncap);
        if (!np) return -1;
        b->p = np; b->cap = ncap;
    }
    b->p[b->len++] = c;
    return 0;
}

static int buf_puts(struct buf *b, const char *s) {
    for (; *s; s++)
        if (buf_putc(b, *s) < 0) return -1;
    return 0;
}

/* ---- a growable vector of field strings (pre-glob) ---- */
struct vec {
    char  **v;
    size_t  n, cap;
};

static int vec_push(struct vec *vec, char *s) {
    if (vec->n + 1 > vec->cap) {
        size_t ncap = vec->cap ? vec->cap * 2 : 8;
        char **nv = realloc(vec->v, ncap * sizeof(char *));
        if (!nv) return -1;
        vec->v = nv; vec->cap = ncap;
    }
    vec->v[vec->n++] = s;
    return 0;
}

static void vec_free(struct vec *vec) {
    size_t i;
    for (i = 0; i < vec->n; i++) free(vec->v[i]);
    free(vec->v);
    vec->v = NULL; vec->n = vec->cap = 0;
}

static int is_ifs(char c)  { return c == ' ' || c == '\t' || c == '\n'; }
static int is_meta(char c) {
    return c == '|' || c == '&' || c == ';' ||
           c == '<' || c == '>' ||
           c == '(' || c == ')' || c == '{' || c == '}';
}
static int name_char(char c, int first) {
    if (c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return 1;
    if (!first && c >= '0' && c <= '9') return 1;
    return 0;
}

/* Flush the working buffer as one field into `fields`.  Resets the buffer.
 * `have` tracks whether anything (even empty, e.g. "") has been started. */
static int flush_field(struct buf *b, int *have, struct vec *fields) {
    char *s;
    if (!*have) return 0;
    s = malloc(b->len + 1);
    if (!s) return -1;
    memcpy(s, b->p, b->len);
    s[b->len] = '\0';
    b->len = 0;
    *have = 0;
    return vec_push(fields, s);
}

/* Append the value of an unquoted parameter expansion, honoring field
 * splitting: IFS runs inside the value break it into separate fields. */
static int append_split(struct buf *b, int *have, struct vec *fields,
                        const char *val) {
    const char *q = val;
    int in_ws_only_pending = 0;
    (void)in_ws_only_pending;
    while (*q) {
        if (is_ifs(*q)) {
            /* end the current field; collapse the IFS run */
            if (flush_field(b, have, fields) < 0) return -1;
            while (is_ifs(*q)) q++;
            continue;
        }
        *have = 1;
        if (buf_putc(b, *q) < 0) return -1;
        q++;
    }
    return 0;
}

void wordfree(wordexp_t *we) {
    size_t i, tot;
    if (!we || !we->we_wordv) return;
    tot = we->we_offs + we->we_wordc;
    for (i = we->we_offs; i < tot; i++)
        free(we->we_wordv[i]);
    free(we->we_wordv);
    we->we_wordv = NULL;
    we->we_wordc = 0;
}

int wordexp(const char *words, wordexp_t *we, int flags) {
    struct buf  b      = {0};
    struct vec  fields = {0};
    int         have   = 0;     /* a field is being assembled */
    int         in_sq  = 0, in_dq = 0;
    int         at_field_start = 1;   /* for tilde recognition */
    const char *p;
    int         rc = WRDE_NOSPACE;
    size_t      i;

    if (flags & WRDE_REUSE) wordfree(we);

    /* ---- pass 1: tilde / parameter expansion, quote removal, splitting ---- */
    for (p = words; *p; p++) {
        char c = *p;

        if (in_sq) {
            if (c == '\'') { in_sq = 0; continue; }
            have = 1; at_field_start = 0;
            if (buf_putc(&b, c) < 0) goto out;
            continue;
        }

        if (c == '\\') {
            if (p[1]) {
                /* In double quotes a backslash only escapes $ ` " \ <newline>. */
                if (in_dq && !(p[1]=='$'||p[1]=='`'||p[1]=='"'||p[1]=='\\'||p[1]=='\n')) {
                    have = 1; at_field_start = 0;
                    if (buf_putc(&b, c) < 0) goto out;
                    continue;
                }
                p++;
                if (*p == '\n') continue;          /* line continuation */
                have = 1; at_field_start = 0;
                if (buf_putc(&b, *p) < 0) goto out;
            }
            continue;
        }

        if (c == '\'' && !in_dq) { in_sq = 1; have = 1; at_field_start = 0; continue; }
        if (c == '"') { in_dq = !in_dq; have = 1; at_field_start = 0; continue; }

        if (c == '`') { rc = WRDE_CMDSUB; goto out; }

        if (c == '$') {
            char name[256];
            size_t nl = 0;
            const char *val;

            if (p[1] == '(') { rc = WRDE_CMDSUB; goto out; }   /* $( and $(( */

            if (p[1] == '$') {                                 /* $$ -> our PID */
                char pid[16];
                long v = (long)getpid();
                int  k = 0;
                if (v == 0) pid[k++] = '0';
                else { char t[16]; int j = 0;
                       while (v > 0) { t[j++] = '0' + (int)(v % 10); v /= 10; }
                       while (j > 0) pid[k++] = t[--j]; }
                pid[k] = '\0';
                p++;
                have = 1; at_field_start = 0;
                if (buf_puts(&b, pid) < 0) goto out;
                continue;
            }

            if (p[1] == '{') {
                p += 2;
                while (*p && *p != '}') {
                    if (nl < sizeof(name) - 1) name[nl++] = *p;
                    p++;
                }
                if (*p != '}') { rc = WRDE_SYNTAX; goto out; }  /* unbalanced ${ */
            } else if (name_char(p[1], 1)) {
                p++;
                while (name_char(*p, 0)) {
                    if (nl < sizeof(name) - 1) name[nl++] = *p;
                    p++;
                }
                p--;   /* loop's p++ will re-consume the terminator */
            } else {
                /* a lone '$' is a literal dollar sign */
                have = 1; at_field_start = 0;
                if (buf_putc(&b, '$') < 0) goto out;
                continue;
            }
            name[nl] = '\0';
            val = getenv(name);
            if (!val) {
                if (flags & WRDE_UNDEF) { rc = WRDE_BADVAL; goto out; }
                val = "";
            }
            at_field_start = 0;
            if (in_dq) {                       /* quoted: no splitting */
                have = 1;
                if (buf_puts(&b, val) < 0) goto out;
            } else {
                if (append_split(&b, &have, &fields, val) < 0) goto out;
            }
            continue;
        }

        if (!in_dq && is_ifs(c)) {
            if (flush_field(&b, &have, &fields) < 0) goto out;
            at_field_start = 1;
            continue;
        }

        if (!in_dq && is_meta(c)) { rc = WRDE_BADCHAR; goto out; }

        if (c == '~' && !in_dq && at_field_start) {
            /* gather the tilde-prefix up to '/' or IFS or end */
            const char *q = p + 1;
            char user[256];
            size_t ul = 0;
            const char *home = NULL;
            while (*q && *q != '/' && !is_ifs(*q) && *q != ':') {
                if (ul < sizeof(user) - 1) user[ul++] = *q;
                q++;
            }
            user[ul] = '\0';
            if (ul == 0) {
                home = getenv("HOME");
            } else {
                struct passwd *pw = getpwnam(user);
                if (pw) home = pw->pw_dir;
            }
            if (home) {
                have = 1; at_field_start = 0;
                if (buf_puts(&b, home) < 0) goto out;
                p = q - 1;       /* resume at the '/' or separator */
                continue;
            }
            /* no expansion — emit the tilde literally and fall through */
        }

        have = 1; at_field_start = 0;
        if (buf_putc(&b, c) < 0) goto out;
    }

    if (in_sq || in_dq) { rc = WRDE_SYNTAX; goto out; }
    if (flush_field(&b, &have, &fields) < 0) goto out;

    /* ---- pass 2: pathname (glob) expansion of each field ---- */
    {
        glob_t g;
        int    gflags = GLOB_NOCHECK | GLOB_NOSORT;
        int    first  = 1;

        memset(&g, 0, sizeof g);
        if (flags & WRDE_DOOFFS) {
            g.gl_offs = we->we_offs;
            gflags |= GLOB_DOOFFS;
        }
        /* When appending, seed glob with the caller's existing vector so its
         * own GLOB_APPEND accounting stays consistent. */
        if (flags & WRDE_APPEND) {
            /* fold the previous results in by re-globbing each as a literal */
        }

        for (i = 0; i < fields.n; i++) {
            int r = glob(fields.v[i], gflags | (first ? 0 : GLOB_APPEND), NULL, &g);
            if (r != 0 && r != GLOB_NOMATCH) {
                globfree(&g);
                rc = (r == GLOB_NOSPACE) ? WRDE_NOSPACE : WRDE_SYNTAX;
                goto out;
            }
            first = 0;
        }

        /* If there were no input fields at all, produce nothing. */
        if (fields.n == 0) {
            g.gl_pathc = 0;
            if (!g.gl_pathv) {
                g.gl_pathv = calloc((flags & WRDE_DOOFFS ? we->we_offs : 0) + 1,
                                    sizeof(char *));
                if (!g.gl_pathv) { rc = WRDE_NOSPACE; goto out; }
            }
        }

        /* Hand glob's result vector to the caller.  glob() already laid it out
         * exactly as wordexp wants: gl_offs leading slots + matches + trailing
         * NULL, with each string malloc'd. */
        if (flags & WRDE_APPEND && we->we_wordv) {
            /* merge: append glob results to the existing vector */
            size_t old = we->we_wordc, add = g.gl_pathc, off = we->we_offs;
            char **merged = realloc(we->we_wordv,
                                    (off + old + add + 1) * sizeof(char *));
            if (!merged) { globfree(&g); rc = WRDE_NOSPACE; goto out; }
            for (i = 0; i < add; i++)
                merged[off + old + i] = g.gl_pathv[g.gl_offs + i];
            merged[off + old + add] = NULL;
            we->we_wordv = merged;
            we->we_wordc = old + add;
            /* free glob's spine but not the strings we stole */
            free(g.gl_pathv);
        } else {
            we->we_wordv = g.gl_pathv;
            we->we_wordc = g.gl_pathc;
            /* we_offs already set by caller for DOOFFS; mirror it otherwise */
            if (!(flags & WRDE_DOOFFS)) we->we_offs = 0;
        }
    }

    rc = 0;

out:
    free(b.p);
    vec_free(&fields);
    return rc;
}
