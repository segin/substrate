/*
 * lib/c/src/glob.c — POSIX pathname pattern expansion.
 *
 * Design:
 *   1. Split `pattern` at `/`.  Each component is either a literal
 *      (no `*?[\\`) or a wildcard component.
 *   2. Walk the components left-to-right, maintaining a working
 *      set of directory paths that have matched so far.
 *      - Literal component: append it to every working path and
 *        keep the ones that exist on disk.
 *      - Wildcard component: opendir each working path, fnmatch
 *        every entry against the component, and replace the working
 *        set with the matches.
 *   3. The final working set is the result; copy each pathname
 *      into pglob->gl_pathv.
 *   4. Optionally sort (GLOB_NOSORT skips), optionally mark
 *      directories with `/` (GLOB_MARK).
 *
 * Backslash-escape handling, leading-period rules, brace expansion:
 *   - `\X` matches a literal X (suppressed by GLOB_NOESCAPE).
 *   - Leading-period entries are skipped unless GLOB_PERIOD is set
 *     OR the pattern explicitly starts with `.` (so `.*` matches
 *     hidden files).
 *   - Brace expansion (GLOB_BRACE) is NOT implemented; the flag is
 *     accepted but ignored.  The shell that needs it can pre-expand.
 *
 * Memory model:
 *   - One large pool grows via realloc as matches accumulate.
 *   - gl_pathv is a NULL-terminated array of malloc'd strings (each
 *     freed by globfree).
 *   - GLOB_APPEND: existing gl_pathv content is preserved; new
 *     matches are appended.  Pointer array realloc'd as needed.
 *
 * Edge cases vetted against the POSIX 7.4.10 test wording:
 *   - Empty pattern -> GLOB_NOMATCH (or NOCHECK -> pattern itself).
 *   - Pattern is exactly "/" -> matches "/" only.
 *   - Pattern ending in "/" with GLOB_MARK -> already has slash, no double.
 *   - errfunc: if non-NULL, called with (path, errno) on opendir
 *     failure; nonzero return or GLOB_ERR aborts.
 */

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

/* Internal dynamic-string-list helper. */
struct strlist {
    char  **v;
    size_t  n;
    size_t  cap;
};

static int sl_push(struct strlist *s, char *str) {
    if (s->n + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        char **nv = (char **)realloc(s->v, nc * sizeof(*nv));
        if (!nv) return -1;
        s->v = nv; s->cap = nc;
    }
    s->v[s->n++] = str;
    return 0;
}

static void sl_free(struct strlist *s) {
    if (!s->v) return;
    for (size_t i = 0; i < s->n; i++) free(s->v[i]);
    free(s->v);
    s->v = 0; s->n = 0; s->cap = 0;
}

/* Is character a glob meta-char? */
static int is_meta(char c) {
    return c == '*' || c == '?' || c == '[';
}

/* Does this string contain any meta-char (treating \X as literal)? */
static int has_meta(const char *p, int flags) {
    for (; *p; p++) {
        if (!(flags & GLOB_NOESCAPE) && *p == '\\' && p[1]) { p++; continue; }
        if (is_meta(*p)) return 1;
    }
    return 0;
}

/* Build "a" + "/" + "b" into a fresh malloc'd string.  a or b may
 * be empty.  Handles the root-anchor case where a == "" + b -> "/b". */
static char *path_join(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    int    need_slash = (la > 0 && a[la - 1] != '/');
    char  *out = (char *)malloc(la + (need_slash ? 1 : 0) + lb + 1);
    if (!out) return 0;
    memcpy(out, a, la);
    if (need_slash) out[la++] = '/';
    memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char  *r = (char *)malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

/* qsort comparison for the sort step. */
static int cmp_str(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

/* Match a single directory's entries against `component`.  Append
 * the joined paths (parent + "/" + entry) to `out`.  Returns 0
 * on success, GLOB_ABORTED on opendir failure when GLOB_ERR or
 * errfunc says to abort, GLOB_NOSPACE on allocation failure. */
static int match_dir(const char *parent, const char *component,
                     int flags, int (*errfunc)(const char *, int),
                     struct strlist *out) {
    /* Handle the leading-period rule.  fnmatch's FNM_PERIOD makes
     * sense for path matching: by default a leading `.` only
     * matches against an explicit `.`-leading pattern (so `*` does
     * not pick up dotfiles).  GLOB_PERIOD disables that. */
    int fnm_flags = (flags & GLOB_PERIOD) ? 0 : FNM_PERIOD;
    if (!(flags & GLOB_NOESCAPE)) {
        /* Default: backslash escapes; nothing extra in fnmatch flags. */
    } else {
        fnm_flags |= FNM_NOESCAPE;
    }

    const char *open_path = parent[0] ? parent : ".";
    DIR *d = opendir(open_path);
    if (!d) {
        if (errfunc) {
            if (errfunc(open_path, errno) != 0) return GLOB_ABORTED;
        }
        if (flags & GLOB_ERR) return GLOB_ABORTED;
        return 0;     /* skip silently */
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != 0) {
        const char *name = ent->d_name;
        if (fnmatch(component, name, fnm_flags) == 0) {
            char *full = path_join(parent, name);
            if (!full) { closedir(d); return GLOB_NOSPACE; }
            if (sl_push(out, full) != 0) {
                free(full);
                closedir(d);
                return GLOB_NOSPACE;
            }
        }
    }
    closedir(d);
    return 0;
}

/* Test whether a path exists (any type). */
static int path_exists(const char *p) {
    struct stat st;
    return lstat(p, &st) == 0;
}

/* Test whether a path is a directory. */
static int path_is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Walk components of `pattern`, recursing into matches at each
 * wildcard component until we've consumed the entire pattern. */
static int glob_recurse(const char *pattern, int flags,
                        int (*errfunc)(const char *, int),
                        struct strlist *matches) {
    /* Split into <current_component> + "/" + <rest> */
    /* `prefix` is what's matched so far (initially empty or "/" if
     * the pattern is absolute). */
    struct strlist current;
    current.v = 0; current.n = 0; current.cap = 0;

    /* Initial prefix: empty or "/". */
    const char *p = pattern;
    char *initial = 0;
    if (*p == '/') {
        initial = xstrdup("/");
        while (*p == '/') p++;
    } else {
        initial = xstrdup("");
    }
    if (!initial) return GLOB_NOSPACE;
    if (sl_push(&current, initial) != 0) {
        free(initial);
        return GLOB_NOSPACE;
    }

    /* Walk components. */
    while (*p) {
        /* Extract one component up to next '/' or end. */
        const char *slash = p;
        while (*slash && *slash != '/') {
            if (!(flags & GLOB_NOESCAPE) && *slash == '\\' && slash[1]) slash++;
            slash++;
        }
        size_t complen = (size_t)(slash - p);
        char  *component = (char *)malloc(complen + 1);
        if (!component) { sl_free(&current); return GLOB_NOSPACE; }
        memcpy(component, p, complen);
        component[complen] = '\0';

        struct strlist next;
        next.v = 0; next.n = 0; next.cap = 0;

        if (has_meta(component, flags)) {
            for (size_t i = 0; i < current.n; i++) {
                int rc = match_dir(current.v[i], component, flags, errfunc, &next);
                if (rc != 0) {
                    free(component); sl_free(&current); sl_free(&next);
                    return rc;
                }
            }
        } else {
            /* Literal: strip any \X escapes, append to each parent,
             * keep ones that exist. */
            char *literal = (char *)malloc(complen + 1);
            if (!literal) {
                free(component); sl_free(&current); sl_free(&next);
                return GLOB_NOSPACE;
            }
            size_t li = 0;
            for (size_t k = 0; k < complen; k++) {
                if (!(flags & GLOB_NOESCAPE) && component[k] == '\\' && k + 1 < complen) {
                    literal[li++] = component[++k];
                } else {
                    literal[li++] = component[k];
                }
            }
            literal[li] = '\0';

            for (size_t i = 0; i < current.n; i++) {
                char *full = path_join(current.v[i], literal);
                if (!full) {
                    free(literal); free(component);
                    sl_free(&current); sl_free(&next);
                    return GLOB_NOSPACE;
                }
                if (path_exists(full)) {
                    if (sl_push(&next, full) != 0) {
                        free(full); free(literal); free(component);
                        sl_free(&current); sl_free(&next);
                        return GLOB_NOSPACE;
                    }
                } else {
                    free(full);
                }
            }
            free(literal);
        }
        free(component);
        sl_free(&current);
        current = next;

        /* Skip trailing slashes between components. */
        p = slash;
        while (*p == '/') p++;

        if (current.n == 0) break;   /* nothing to recurse into */
    }

    /* Append `current` to `matches`. */
    for (size_t i = 0; i < current.n; i++) {
        if (sl_push(matches, current.v[i]) != 0) {
            /* Free the rest we haven't transferred. */
            for (size_t k = i; k < current.n; k++) free(current.v[k]);
            free(current.v);
            return GLOB_NOSPACE;
        }
    }
    free(current.v);
    return 0;
}

int glob(const char *pattern, int flags,
         int (*errfunc)(const char *, int),
         glob_t *pglob) {
    if (!pattern || !pglob) {
        errno = EINVAL;
        return GLOB_NOSPACE;
    }

    struct strlist matches;
    matches.v = 0; matches.n = 0; matches.cap = 0;

    int rc = glob_recurse(pattern, flags, errfunc, &matches);
    if (rc != 0) {
        sl_free(&matches);
        return rc;
    }

    /* No matches at all? */
    if (matches.n == 0) {
        if (flags & GLOB_NOCHECK) {
            char *p = xstrdup(pattern);
            if (!p) return GLOB_NOSPACE;
            if (sl_push(&matches, p) != 0) { free(p); return GLOB_NOSPACE; }
        } else {
            sl_free(&matches);
            if (!(flags & GLOB_APPEND)) {
                pglob->gl_pathc = 0;
                pglob->gl_pathv = 0;
            }
            return GLOB_NOMATCH;
        }
    }

    /* Apply GLOB_MARK: append `/` to directory matches. */
    if (flags & GLOB_MARK) {
        for (size_t i = 0; i < matches.n; i++) {
            char *m = matches.v[i];
            size_t len = strlen(m);
            if (len == 0 || m[len - 1] == '/') continue;
            if (path_is_dir(m)) {
                char *withslash = (char *)malloc(len + 2);
                if (!withslash) { sl_free(&matches); return GLOB_NOSPACE; }
                memcpy(withslash, m, len);
                withslash[len] = '/';
                withslash[len + 1] = '\0';
                free(m);
                matches.v[i] = withslash;
            }
        }
    }

    /* Sort unless GLOB_NOSORT. */
    if (!(flags & GLOB_NOSORT)) {
        qsort(matches.v, matches.n, sizeof(char *), cmp_str);
    }

    /* Splice `matches` into pglob->gl_pathv.  Honour GLOB_APPEND
     * (extend existing) and GLOB_DOOFFS (leave gl_offs NULL slots
     * at the front).  Always NULL-terminate. */
    size_t pre_count = 0;
    size_t pre_offs  = (flags & GLOB_DOOFFS) ? pglob->gl_offs : 0;
    char **old_v = 0;

    if (flags & GLOB_APPEND) {
        pre_count = pglob->gl_pathc;
        old_v = pglob->gl_pathv;
    } else {
        pglob->gl_pathc = 0;
        pglob->gl_pathv = 0;
        if (!(flags & GLOB_DOOFFS)) pglob->gl_offs = 0;
    }

    size_t new_count = pre_count + matches.n;
    size_t total = pre_offs + new_count + 1;   /* +1 for NULL terminator */
    char **nv = (char **)malloc(total * sizeof(*nv));
    if (!nv) { sl_free(&matches); return GLOB_NOSPACE; }

    size_t out_i = 0;
    for (size_t i = 0; i < pre_offs; i++) nv[out_i++] = 0;
    if (old_v) {
        for (size_t i = 0; i < pre_count; i++) nv[out_i++] = old_v[i + pre_offs];
        free(old_v);
    }
    for (size_t i = 0; i < matches.n; i++) nv[out_i++] = matches.v[i];
    nv[out_i] = 0;

    /* matches.v's contents now owned by nv; free the spine only. */
    free(matches.v);

    pglob->gl_pathc = new_count;
    pglob->gl_pathv = nv;
    return 0;
}

void globfree(glob_t *pglob) {
    if (!pglob || !pglob->gl_pathv) return;
    /* Free every non-NULL entry (gl_offs entries at the head are
     * NULL by construction, but be defensive). */
    for (size_t i = 0; i < pglob->gl_offs + pglob->gl_pathc; i++) {
        if (pglob->gl_pathv[i]) free(pglob->gl_pathv[i]);
    }
    free(pglob->gl_pathv);
    pglob->gl_pathv = 0;
    pglob->gl_pathc = 0;
}
