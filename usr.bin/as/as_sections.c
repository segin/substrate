#include "as_sections.h"

#include "as_strmap.h"
#include "as_util.h"
#include "elfobj.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef struct {
    as_section_state_t *out;
    char *errbuf;
    size_t errbuf_sz;
    /* Which section the source is in, the one before it and what is
     * pushed: the one tracker of that there is, which the emitter walks
     * a source with too.  The indices in `out` are where it says. */
    section_track_t track;
} sec_ctx_t;

/*
 * Room for n things of `size` bytes each where `old` was, or NULL: for
 * want of memory, or for a count the multiplication cannot hold.  Every
 * table here that grows, grows by this: nine did, each multiplying a
 * doubled count by a size with nothing to say the product was one.
 */
static void *realloc_array(void *old, size_t n, size_t size) {
    if (size != 0 && n > SIZE_MAX / size) {
        return NULL;
    }
    return realloc(old, n * size);
}

static char *unquote_copy(const char *s) {
    size_t n;
    char *tmp;

    tmp = trim_copy(s);
    if (tmp == NULL) {
        return NULL;
    }
    n = strlen(tmp);
    if (n >= 2 && ((tmp[0] == '"' && tmp[n - 1] == '"') || (tmp[0] == '\'' && tmp[n - 1] == '\''))) {
        memmove(tmp, tmp + 1, n - 2);
        tmp[n - 2] = '\0';
    }
    return tmp;
}

void as_section_state_init(as_section_state_t *s) {
    if (s == NULL) {
        return;
    }
    s->items = NULL;
    s->count = 0;
    s->cap = 0;
    s->current_index = 0;
    s->previous_index = 0;
    s->elf64 = 0;
    memset(&s->by_name, 0, sizeof(s->by_name));
}

void as_section_state_free(as_section_state_t *s) {
    size_t i;

    if (s == NULL) {
        return;
    }
    for (i = 0; i < s->count; ++i) {
        free(s->items[i].name);
        free(s->items[i].group);
    }
    free(s->items);
    as_strmap_free(&s->by_name);
    memset(s, 0, sizeof(*s));
}

const as_section_t *as_sections_find(const as_section_state_t *s, const char *name, unsigned subsection) {
    size_t i;

    if (s == NULL || name == NULL) {
        return NULL;
    }
    if (subsection == 0) {
        return as_strmap_get(&s->by_name, name, &i) ? &s->items[i] : NULL;
    }

    for (i = 0; i < s->count; ++i) {
        if (s->items[i].subsection == subsection && strcmp(s->items[i].name, name) == 0) {
            return &s->items[i];
        }
    }
    return NULL;
}

static ssize_t find_section_index(const as_section_state_t *s, const char *name, unsigned subsection) {
    size_t i;

    if (subsection == 0) {
        return as_strmap_get(&s->by_name, name, &i) ? (ssize_t)i : -1;
    }
    for (i = 0; i < s->count; ++i) {
        if (s->items[i].subsection == subsection && strcmp(s->items[i].name, name) == 0) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static ssize_t add_section(as_section_state_t *s, const char *name, unsigned subsection,
                           unsigned flags, unsigned type, unsigned align) {
    as_section_t *next;

    if (s->count == s->cap) {
        size_t ncap = s->cap == 0 ? 16 : s->cap * 2;
        next = (as_section_t *)realloc_array(s->items, ncap, sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        s->items = next;
        s->cap = ncap;
    }

    memset(&s->items[s->count], 0, sizeof(s->items[s->count]));
    s->items[s->count].name = xstrdup(name);
    if (s->items[s->count].name == NULL) {
        return -1;
    }
    if (subsection == 0 && as_strmap_put(&s->by_name, s->items[s->count].name, s->count) != 0) {
        free(s->items[s->count].name);
        return -1;
    }
    s->items[s->count].subsection = subsection;
    s->items[s->count].flags = flags;
    s->items[s->count].type = type;
    s->items[s->count].align = align;
    s->items[s->count].entsize = 0;
    s->count++;
    return (ssize_t)(s->count - 1);
}

static int ensure_builtins(as_section_state_t *s) {
    /*
     * The three every object has, each aligned as far as the source
     * asks and no further: they were 16, 4 and 4 whatever it asked, and
     * an empty .rodata was a fourth.
     */
    if (add_section(s, ".text", 0, SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 1) < 0) {
        return -1;
    }
    if (add_section(s, ".data", 0, SHF_ALLOC | SHF_WRITE, SHT_PROGBITS, 1) < 0) {
        return -1;
    }
    if (add_section(s, ".bss", 0, SHF_ALLOC | SHF_WRITE, SHT_NOBITS, 1) < 0) {
        return -1;
    }
    s->current_index = 0;
    s->previous_index = 0;
    return 0;
}

static int parse_u32_arg(const char *raw, unsigned *out) {
    long long v;

    /* An expression of numbers alone, whose value fits 32 bits unsigned. */
    if (raw == NULL || out == NULL ||
        as_expr_eval_string(raw, NULL, NULL, &v) != AS_EXPR_EVAL_OK ||
        v < 0 || v > (long long)UINT_MAX) {
        return -1;
    }
    *out = (unsigned)v;
    return 0;
}

/*
 * The flags a .section's string asks for: a letter for each, or a number
 * for bits that have no letter.  `?` is no flag of the section's own: it
 * asks to join the group of the section it is written in, and sets
 * *join_group.  A letter that is none of these was passed over, and the
 * section made without what it asked for; it is refused (-1), with the
 * letter in *bad.
 */
static int parse_flags_string(const char *raw, unsigned *flags_out, int *join_group, char *bad) {
    char *f;
    size_t i;
    unsigned flags = 0;

    *join_group = 0;
    *bad = '\0';
    f = unquote_copy(raw);
    if (f == NULL) {
        return -1;
    }
    for (i = 0; f[i] != '\0'; ++i) {
        switch (f[i]) {
        case 'a':
            flags |= SHF_ALLOC;
            break;
        case 'w':
            flags |= SHF_WRITE;
            break;
        case 'x':
            flags |= SHF_EXECINSTR;
            break;
        case 'M':
            flags |= SHF_MERGE;
            break;
        case 'S':
            flags |= SHF_STRINGS;
            break;
        case 'G':
        case 'g':
            flags |= SHF_GROUP;
            break;
        case 'T':
            flags |= SHF_TLS;
            break;
        case 'o':
            flags |= SHF_LINK_ORDER;
            break;
        case 'e':
            flags |= SHF_EXCLUDE;
            break;
        case 'R':
            flags |= SHF_GNU_RETAIN;
            break;
        case 'l':
            flags |= SHF_X86_64_LARGE;
            break;
        case '?':
            *join_group = 1;
            break;
        default:
            if (f[i] >= '0' && f[i] <= '9') {
                char *end = NULL;
                unsigned long bits = strtoul(f + i, &end, 0);

                flags |= (unsigned)bits;
                i = (size_t)(end - f) - 1;
                break;
            }
            *bad = f[i];
            free(f);
            return -1;
        }
    }
    free(f);
    *flags_out = flags;
    return 0;
}

static unsigned parse_type_string(const char *raw, int is_64) {
    char *t;
    char *s;
    unsigned type = SHT_PROGBITS;

    t = trim_copy(raw);
    if (t == NULL) {
        return SHT_PROGBITS;
    }
    s = t;
    while (*s == '@' || *s == '%') {
        s++;
    }

    if (*s == '"') {
        size_t n;

        s++;
        n = strlen(s);
        if (n > 0 && s[n - 1] == '"') {
            s[n - 1] = '\0';
        }
    }
    if (streq_ci(s, "progbits")) {
        type = SHT_PROGBITS;
    } else if (streq_ci(s, "nobits")) {
        type = SHT_NOBITS;
    } else if (streq_ci(s, "note")) {
        type = SHT_NOTE;
    } else if (streq_ci(s, "init_array")) {
        type = SHT_INIT_ARRAY;
    } else if (streq_ci(s, "fini_array")) {
        type = SHT_FINI_ARRAY;
    } else if (streq_ci(s, "preinit_array")) {
        type = SHT_PREINIT_ARRAY;
    } else if (streq_ci(s, "unwind")) {
        type = is_64 ? SHT_X86_64_UNWIND : SHT_PROGBITS;
    } else if (*s >= '0' && *s <= '9') {
        type = (unsigned)strtoul(s, NULL, 0);
    } else if (streq_ci(s, "rela")) {
        type = SHT_RELA;
    } else if (streq_ci(s, "rel")) {
        type = SHT_REL;
    }

    free(t);
    return type;
}

/*
 * What a section is when it is opened by name alone.  The names that
 * mean something have the flags and type GNU as 2.46 gives them; any
 * other is PROGBITS with no flags -- not allocated, which is to say not
 * in the program.  It was given "a", and of the names below only .text,
 * .data, .bss and .rodata were known, so .init and .fini were not code,
 * .tdata and .tbss not thread-local, and .init_array an array of
 * nothing in particular that the linker did not run.
 *
 * `how` says what of the name is matched: the whole of it; the name and
 * anything after a dot; or the name as a prefix.
 */
enum { SEC_NAME_EXACT, SEC_NAME_DOTTED, SEC_NAME_PREFIX };

static void infer_section_defaults(const char *name, unsigned *flags, unsigned *type) {
    static const struct {
        const char *name;
        int how;
        unsigned type;
        unsigned flags;
    } known[] = {
        {".text", SEC_NAME_DOTTED, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR},
        {".init", SEC_NAME_EXACT, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR},
        {".fini", SEC_NAME_EXACT, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR},
        {".plt", SEC_NAME_EXACT, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR},
        {".data", SEC_NAME_DOTTED, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE},
        {".data1", SEC_NAME_EXACT, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE},
        {".got", SEC_NAME_EXACT, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE},
        {".persistent", SEC_NAME_EXACT, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE},
        {".bss", SEC_NAME_DOTTED, SHT_NOBITS, SHF_ALLOC | SHF_WRITE},
        {".noinit", SEC_NAME_EXACT, SHT_NOBITS, SHF_ALLOC | SHF_WRITE},
        {".gnu.linkonce.b", SEC_NAME_PREFIX, SHT_NOBITS, SHF_ALLOC | SHF_WRITE},
        {".rodata", SEC_NAME_DOTTED, SHT_PROGBITS, SHF_ALLOC},
        {".rodata1", SEC_NAME_EXACT, SHT_PROGBITS, SHF_ALLOC},
        {".tdata", SEC_NAME_DOTTED, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS},
        {".tbss", SEC_NAME_DOTTED, SHT_NOBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS},
        {".init_array", SEC_NAME_DOTTED, SHT_INIT_ARRAY, SHF_ALLOC | SHF_WRITE},
        {".fini_array", SEC_NAME_DOTTED, SHT_FINI_ARRAY, SHF_ALLOC | SHF_WRITE},
        {".preinit_array", SEC_NAME_DOTTED, SHT_PREINIT_ARRAY, SHF_ALLOC | SHF_WRITE},
        {".note", SEC_NAME_PREFIX, SHT_NOTE, 0},
        {".dynamic", SEC_NAME_EXACT, SHT_DYNAMIC, SHF_ALLOC},
        {".hash", SEC_NAME_EXACT, SHT_HASH, SHF_ALLOC},
        {".dynstr", SEC_NAME_EXACT, SHT_STRTAB, SHF_ALLOC},
        {".dynsym", SEC_NAME_EXACT, SHT_DYNSYM, SHF_ALLOC},
        {".stabstr", SEC_NAME_EXACT, SHT_STRTAB, 0},
    };
    unsigned f = 0;
    unsigned t = SHT_PROGBITS;
    size_t i;

    for (i = 0; name != NULL && i < sizeof(known) / sizeof(known[0]); ++i) {
        size_t n = strlen(known[i].name);

        if (strncmp(name, known[i].name, n) != 0) {
            continue;
        }
        if (name[n] == '\0' || known[i].how == SEC_NAME_PREFIX ||
            (known[i].how == SEC_NAME_DOTTED && name[n] == '.')) {
            f = known[i].flags;
            t = known[i].type;
            break;
        }
    }

    if (flags != NULL) {
        *flags = f;
    }
    if (type != NULL) {
        *type = t;
    }
}

const char *section_from_directive(const as_directive_t *d) {
    const char *arg;
    /* The name out of its quotes, good until the next is asked for; as
     * long as the name is, where it had been 255 bytes of it. */
    static char *secbuf;
    static size_t secbuf_cap;
    size_t i;
    size_t n = 0;

    if (d == NULL || d->name == NULL) {
        return NULL;
    }
    if (as_strcmp(d->name, ".text") == 0) return ".text";
    if (as_strcmp(d->name, ".data") == 0) return ".data";
    if (as_strcmp(d->name, ".rodata") == 0) return ".rodata";
    if (as_strcmp(d->name, ".bss") == 0) return ".bss";
    if ((as_strcmp(d->name, ".section") == 0 || as_strcmp(d->name, ".pushsection") == 0) &&
        d->arg_count > 0 && d->args[0] != NULL && d->args[0][0] != '\0') {
        arg = d->args[0];
        while (*arg != '\0' && isspace((unsigned char)*arg)) {
            arg++;
        }
        if (strlen(arg) + 1 > secbuf_cap) {
            char *grown = (char *)realloc(secbuf, strlen(arg) + 1);

            if (grown == NULL) {
                return NULL;
            }
            secbuf = grown;
            secbuf_cap = strlen(arg) + 1;
        }
        if (*arg == '"' || *arg == '\'') {
            char q = *arg++;
            while (*arg != '\0' && *arg != q && n + 1 < secbuf_cap) {
                secbuf[n++] = *arg++;
            }
            secbuf[n] = '\0';
            return secbuf;
        }
        /* (The argument is the name, all of it: one written in quotes
         * may have a space in it, and comes here without the quotes.
         * It was cut at the space, and the section the table of them
         * has by its whole name was "unknown section".) */
        for (i = 0; arg[i] != '\0' && n + 1 < secbuf_cap; ++i) {
            if (arg[i] == ',') {
                break;
            }
            secbuf[n++] = arg[i];
        }
        while (n > 0 && isspace((unsigned char)secbuf[n - 1])) {
            n--;
        }
        secbuf[n] = '\0';
        return n > 0 ? secbuf : NULL;
    }
    return NULL;
}

int section_track_init(section_track_t *st, unsigned initial_code_bits) {
    if (st == NULL) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->current = xstrdup(".text");
    st->previous = xstrdup(".text");
    st->x86_code_bits = initial_code_bits == 64u ? 64u : (initial_code_bits == 16u ? 16u : 32u);
    if (st->current == NULL || st->previous == NULL) {
        free(st->current);
        free(st->previous);
        memset(st, 0, sizeof(*st));
        return -1;
    }
    return 0;
}

void section_track_free(section_track_t *st) {
    size_t i;

    if (st == NULL) {
        return;
    }
    for (i = 0; i < st->stack_count; ++i) {
        free(st->stack[i]);
    }
    free(st->stack);
    free(st->current);
    free(st->previous);
    memset(st, 0, sizeof(*st));
}

static int section_track_switch(section_track_t *st, const char *name) {
    char *next;
    char *old;

    if (st == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    next = xstrdup(name);
    if (next == NULL) {
        return -1;
    }
    old = st->current;
    st->current = next;
    free(st->previous);
    st->previous = old;
    return 0;
}

static int section_track_push(section_track_t *st) {
    char **next;

    if (st == NULL || st->current == NULL) {
        return -1;
    }
    /* Two of each push: the section it was written in, and the one
     * before that, which a .popsection brings back as well. */
    if (st->stack_count + 2 > st->stack_cap) {
        size_t ncap = st->stack_cap == 0 ? 8 : st->stack_cap * 2;
        next = (char **)realloc(st->stack, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        st->stack = next;
        st->stack_cap = ncap;
    }
    st->stack[st->stack_count] = xstrdup(st->current);
    if (st->stack[st->stack_count] == NULL) {
        return -1;
    }
    st->stack[st->stack_count + 1] = st->previous != NULL ? xstrdup(st->previous) : NULL;
    if (st->previous != NULL && st->stack[st->stack_count + 1] == NULL) {
        free(st->stack[st->stack_count]);
        return -1;
    }
    st->stack_count += 2;
    return 0;
}

int section_track_apply_directive(section_track_t *st, const as_directive_t *d) {
    const char *sec_name;
    char *tmp;

    if (st == NULL || d == NULL || d->name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".previous") == 0) {
        if (st->previous == NULL) {
            return 1;
        }
        tmp = st->current;
        st->current = st->previous;
        st->previous = tmp;
        return 1;
    }
    if (as_strcmp(d->name, ".popsection") == 0) {
        if (st->stack_count < 2) {
            return 1;
        }
        st->stack_count -= 2;
        free(st->current);
        free(st->previous);
        st->current = st->stack[st->stack_count];
        st->previous = st->stack[st->stack_count + 1];
        return 1;
    }
    if (as_strcmp(d->name, ".code16") == 0) {
        st->x86_code_bits = 16u;
        return 1;
    }
    if (as_strcmp(d->name, ".code32") == 0) {
        st->x86_code_bits = 32u;
        return 1;
    }
    if (as_strcmp(d->name, ".code64") == 0) {
        st->x86_code_bits = 64u;
        return 1;
    }
    sec_name = section_from_directive(d);
    if (sec_name == NULL) {
        return 0;
    }
    if (as_strcmp(d->name, ".pushsection") == 0 && section_track_push(st) != 0) {
        return -1;
    }
    if (section_track_switch(st, sec_name) != 0) {
        return -1;
    }
    return 1;
}

static int switch_section(sec_ctx_t *ctx, const char *name, unsigned subsection,
                          unsigned flags, unsigned type, int update_attrs) {
    ssize_t idx;
    as_section_state_t *s = ctx->out;

    idx = find_section_index(s, name, subsection);
    if (idx < 0) {
        idx = add_section(s, name, subsection, flags, type, 1);
        if (idx < 0) {
            return -1;
        }
    }

    if (update_attrs) {
        s->items[idx].flags = flags;
        s->items[idx].type = type;
    }

    if (section_track_switch(&ctx->track, name) != 0) {
        return -1;
    }
    s->previous_index = s->current_index;
    s->current_index = (size_t)idx;
    return 0;
}

/* The indices of the section the tracker is in and of the one before
 * it: after a .popsection or a .previous, which name no section. */
static void follow_track(sec_ctx_t *ctx) {
    as_section_state_t *s = ctx->out;
    ssize_t cur = ctx->track.current != NULL ? find_section_index(s, ctx->track.current, 0) : -1;
    ssize_t prev = ctx->track.previous != NULL ? find_section_index(s, ctx->track.previous, 0) : -1;

    if (cur >= 0) {
        s->current_index = (size_t)cur;
    }
    if (prev >= 0) {
        s->previous_index = (size_t)prev;
    }
}

/*
 * What a directive handler returns for a subsection other than 0.  The
 * contents of subsection n of a section belong after those of every lower
 * one, wherever in the source they were written.  They are put there by
 * as_sections_gather_subsections, which takes the numbers away as it
 * does; a number that reaches here was not gathered, and to go on would
 * assemble out-of-line code in line.
 */
#define SEC_NO_SUBSECTIONS (-2)
/* The handler has said what is wrong. */
#define SEC_SAID (-3)

static int process_section_like(sec_ctx_t *ctx, const as_stmt_t *st, int do_push) {
    const as_directive_t *d = &st->u.directive;
    as_section_state_t *s = ctx->out;
    char *name;
    char *group = NULL;
    unsigned flags;
    unsigned type;
    ssize_t existing = -1;
    long long subsection_number;
    int join_group = 0;
    char *joined_group = NULL;
    int joined_comdat = 0;
    unsigned entsize = 0;
    size_t next_arg;

    if (d->arg_count < 1) {
        return -1;
    }

    /* The section it is written in and the one before: a .popsection
     * brings both back. */
    if (do_push && section_track_push(&ctx->track) != 0) {
        return -1;
    }

    name = trim_copy(d->args[0]);
    if (name == NULL) {
        return -1;
    }

    existing = find_section_index(s, name, 0);
    /*
     * `.pushsection name, 1`: a number after the name is a subsection, not
     * a string of flags.  Read as flags it was no flags at all, and was
     * then written over those of the section: .text lost its "ax" and
     * every instruction in it with them.  Subsection 0 is the section,
     * entered with what it had; another is refused (SEC_NO_SUBSECTIONS).
     */
    if (do_push && d->arg_count == 2 && d->args[1] != NULL &&
        strchr(d->args[1], '"') == NULL &&
        as_expr_eval_string(d->args[1], NULL, NULL, &subsection_number) == AS_EXPR_EVAL_OK) {
        if (subsection_number != 0) {
            free(name);
            return SEC_NO_SUBSECTIONS;
        }
        if (existing >= 0) {
            flags = s->items[existing].flags;
            type = s->items[existing].type;
        } else {
            infer_section_defaults(name, &flags, &type);
        }
        if (switch_section(ctx, name, 0, flags, type, existing < 0) != 0) {
            free(name);
            return -1;
        }
        free(name);
        return 0;
    }
    if (d->arg_count >= 2) {
        char bad;

        int rc = parse_flags_string(d->args[1], &flags, &join_group, &bad);

        /* `l`, a large section, is of 64-bit objects alone. */
        if (rc == 0 && (flags & SHF_X86_64_LARGE) != 0 && !s->elf64 && d->args[1] != NULL &&
            strchr(d->args[1], 'l') != NULL) {
            bad = 'l';
            rc = -1;
        }
        if (rc != 0) {
            set_err(ctx, "%s:%u: %s %s: '%c' is not a section flag (a, w, x, M, S, G, T, o, e, R, l, ? or a number)",
                    st->file != NULL ? st->file : "<input>", st->line, d->name, name, bad != '\0' ? bad : '?');
            free(name);
            return SEC_SAID;
        }
        /*
         * A section is what it was first said to be.  Flags that are
         * not those it has were written over them, so that whichever
         * directive came last decided: GNU as refuses the change, and so
         * does this.
         */
        if (existing >= 0 && s->items[existing].flags != (flags | (s->items[existing].flags & SHF_GROUP)) &&
            s->items[existing].flags != flags) {
            set_err(ctx, "%s:%u: %s %s: the section has other flags already",
                    st->file != NULL ? st->file : "<input>", st->line, d->name, name);
            free(name);
            return SEC_SAID;
        }
    } else if (existing >= 0) {
        flags = s->items[existing].flags;
    } else {
        infer_section_defaults(name, &flags, NULL);
    }

    if (d->arg_count >= 3 && d->args[2] != NULL && d->args[2][0] != '\0') {
        type = parse_type_string(d->args[2], s->elf64);
        /* Nor is its type another's the second time. */
        if (existing >= 0 && s->items[existing].type != type) {
            set_err(ctx, "%s:%u: %s %s: the section has another type already",
                    st->file != NULL ? st->file : "<input>", st->line, d->name, name);
            free(name);
            return SEC_SAID;
        }
    } else if (existing >= 0) {
        type = s->items[existing].type;
    } else {
        infer_section_defaults(name, NULL, &type);
    }

    /* `?`: of the group the directive is written in, if it is in one. */
    if (join_group && s->items[s->current_index].group != NULL) {
        joined_group = trim_copy(s->items[s->current_index].group);
        joined_comdat = s->items[s->current_index].comdat;
        if (joined_group == NULL) {
            free(name);
            return -1;
        }
    }

    if (switch_section(ctx, name, 0, flags, type, 1) != 0) {
        free(joined_group);
        free(name);
        return -1;
    }
    /* After the type: the size of an entry if the section is of them
     * (M), the symbol it is ordered by (o), and its group (G). */
    next_arg = 3;
    if ((flags & SHF_MERGE) != 0 && d->arg_count > next_arg && parse_u32_arg(d->args[next_arg], &entsize) == 0) {
        if (existing >= 0 && s->items[existing].entsize != 0 && s->items[existing].entsize != entsize) {
            set_err(ctx, "%s:%u: %s %s: the section's entries have another size already",
                    st->file != NULL ? st->file : "<input>", st->line, d->name, name);
            free(name);
            return SEC_SAID;
        }
        s->items[s->current_index].entsize = entsize;
        next_arg++;
    }
    if ((flags & SHF_LINK_ORDER) != 0 && d->arg_count > next_arg) {
        next_arg++;
    }
    /* Entries to be merged have a size, or are not merged. */
    if ((flags & SHF_MERGE) != 0 && s->items[s->current_index].entsize == 0 &&
        (existing < 0 || s->items[existing].entsize == 0)) {
        s->items[s->current_index].flags &= ~(unsigned)SHF_MERGE;
    }
    if (joined_group != NULL) {
        group = joined_group;
    } else if (d->arg_count > next_arg) {
        group = trim_copy(d->args[next_arg]);
        if (group == NULL) {
            free(name);
            return -1;
        }
    } else if (existing >= 0 && s->items[existing].group != NULL) {
        group = trim_copy(s->items[existing].group);
        if (group == NULL) {
            free(name);
            return -1;
        }
    }
    free(s->items[s->current_index].group);
    s->items[s->current_index].group = group;
    if (joined_group != NULL) {
        s->items[s->current_index].comdat = joined_comdat;
    } else if (d->arg_count > next_arg + 1) {
        char *f = trim_copy(d->args[next_arg + 1]);
        if (f == NULL) {
            free(name);
            return -1;
        }
        s->items[s->current_index].comdat = streq_ci(f, "comdat") ? 1 : 0;
        free(f);
    } else if (existing >= 0) {
        s->items[s->current_index].comdat = s->items[existing].comdat;
    }
    if (s->items[s->current_index].group != NULL || s->items[s->current_index].comdat) {
        s->items[s->current_index].flags |= SHF_GROUP;
    }
    if (s->items[s->current_index].entsize == 0 && existing >= 0) {
        s->items[s->current_index].entsize = s->items[existing].entsize;
    }

    free(name);
    return 0;
}

static int process_directive(sec_ctx_t *ctx, const as_stmt_t *st) {
    const as_directive_t *d = &st->u.directive;
    as_section_state_t *s = ctx->out;

    if (strcmp(d->name, ".text") == 0 || strcmp(d->name, ".data") == 0 || strcmp(d->name, ".bss") == 0 ||
        strcmp(d->name, ".rodata") == 0) {
        unsigned sub = 0;
        unsigned flags = 0;
        unsigned type = 0;

        /* `.text 1`: a subsection. */
        if (d->arg_count >= 1 && d->args[0] != NULL && d->args[0][0] != '\0') {
            if (parse_u32_arg(d->args[0], &sub) != 0) {
                return -1;
            }
            if (sub != 0) {
                return SEC_NO_SUBSECTIONS;
            }
        }
        /* .rodata is there once it is named, and is what its name says. */
        infer_section_defaults(d->name, &flags, &type);
        if (switch_section(ctx, d->name, 0, flags, type, 0) != 0) {
            return -1;
        }
        return 0;
    }

    if (strcmp(d->name, ".section") == 0) {
        return process_section_like(ctx, st, 0);
    }

    if (strcmp(d->name, ".pushsection") == 0) {
        return process_section_like(ctx, st, 1);
    }

    if (strcmp(d->name, ".popsection") == 0) {
        /* One with nothing pushed is warned of and taken out before
         * this (as_directives_lower). */
        if (section_track_apply_directive(&ctx->track, d) < 0) {
            return -1;
        }
        follow_track(ctx);
        return 0;
    }

    if (strcmp(d->name, ".previous") == 0) {
        if (section_track_apply_directive(&ctx->track, d) < 0) {
            return -1;
        }
        follow_track(ctx);
        return 0;
    }

    if (strcmp(d->name, ".subsection") == 0) {
        unsigned sub = 0;
        as_section_t *cur;
        if (d->arg_count < 1 || parse_u32_arg(d->args[0], &sub) != 0) {
            return -1;
        }
        if (sub != 0) {
            return SEC_NO_SUBSECTIONS;
        }
        cur = &s->items[s->current_index];
        if (switch_section(ctx, cur->name, sub, cur->flags, cur->type, 1) != 0) {
            return -1;
        }
        if (s->items[s->current_index].align < cur->align) {
            s->items[s->current_index].align = cur->align;
        }
        return 0;
    }

    if (strcmp(d->name, ".group") == 0) {
        as_section_t *cur = &s->items[s->current_index];
        if (d->arg_count >= 1) {
            char *g = trim_copy(d->args[0]);
            if (g == NULL) {
                return -1;
            }
            free(cur->group);
            cur->group = g;
        }
        if (d->arg_count >= 2) {
            char *f = trim_copy(d->args[1]);
            if (f == NULL) {
                return -1;
            }
            if (streq_ci(f, "comdat")) {
                cur->comdat = 1;
                cur->flags |= SHF_GROUP;
            }
            free(f);
        }
        return 0;
    }

    if (strcmp(d->name, ".align") == 0 || strcmp(d->name, ".balign") == 0 || strcmp(d->name, ".p2align") == 0) {
        unsigned a = 0;
        as_section_t *cur = &s->items[s->current_index];
        if (d->arg_count < 1 || parse_u32_arg(d->args[0], &a) != 0) {
            return -1;
        }
        if (strcmp(d->name, ".p2align") == 0) {
            if (a > 31) {
                return -1;
            }
            a = 1u << a;
        }
        if (a > cur->align) {
            cur->align = a;
        }
        return 0;
    }

    return 0;
}

/*
 * Subsections.
 *
 * `.text 1`, `.subsection 1` and `.pushsection name, 1` put what follows
 * in a numbered part of the section, and the section is its parts in
 * ascending order of number whatever the order they were written in.  It
 * is how code is put out of line: written in the middle of a function,
 * assembled after it.
 *
 * Nothing after the parser knows of them.  The statements are put in the
 * order they are to be assembled in, here, once: those of a section's
 * lowest subsection -- 0, unless a negative one is written -- stay where
 * they are, and those of every other are moved to the end of
 * the source, each run of them behind a `.section` that names its
 * section, the runs in order of section and then of number.  A section
 * being its name to everything downstream, what is appended to it last
 * lies last in it.
 */
typedef struct {
    size_t sec;
    int sub;
} sub_where_t;

typedef struct {
    sub_where_t cur;
    sub_where_t prev;
} sub_frame_t;

typedef struct {
    char **names; /* unquoted, in order of first appearance */
    char **spell; /* as first written, for a `.section` to be made of */
    int *low;     /* the lowest subsection of each that is entered, */
    int *high;    /* and the highest */
    size_t count;
    size_t cap;
    as_strmap_t by_name; /* where each of `names` is */
    sub_frame_t at;
    sub_frame_t *stack;
    size_t stack_count;
    size_t stack_cap;
} sub_track_t;

typedef struct {
    sub_where_t where;
    size_t seq;
    as_stmt_t st;
} sub_moved_t;

typedef struct {
    as_stmt_t *items;
    size_t count;
    size_t cap;
} sub_stmt_vec_t;

static size_t sub_intern(sub_track_t *t, const char *raw) {
    char *name = unquote_copy(raw);
    size_t i;

    if (name == NULL) {
        return (size_t)-1;
    }
    /* (By the table of them: each was looked for among all there are,
     * at every `.section`.) */
    if (as_strmap_get(&t->by_name, name, &i)) {
        free(name);
        return i;
    }
    if (t->count == t->cap) {
        size_t ncap = t->cap == 0 ? 8 : t->cap * 2;
        char **names = (char **)realloc_array(t->names, ncap, sizeof(*names));
        char **spell;

        if (names == NULL) {
            free(name);
            return (size_t)-1;
        }
        t->names = names;
        spell = (char **)realloc_array(t->spell, ncap, sizeof(*spell));
        if (spell == NULL) {
            free(name);
            return (size_t)-1;
        }
        t->spell = spell;
        {
            int *low = (int *)realloc_array(t->low, ncap, sizeof(*low));
            int *high;

            if (low == NULL) {
                free(name);
                return (size_t)-1;
            }
            t->low = low;
            high = (int *)realloc_array(t->high, ncap, sizeof(*high));
            if (high == NULL) {
                free(name);
                return (size_t)-1;
            }
            t->high = high;
        }
        t->cap = ncap;
    }
    t->spell[t->count] = trim_copy(raw);
    if (t->spell[t->count] == NULL || as_strmap_put(&t->by_name, name, t->count) != 0) {
        free(t->spell[t->count]);
        free(name);
        return (size_t)-1;
    }
    t->names[t->count] = name;
    t->low[t->count] = INT_MAX;
    t->high[t->count] = INT_MIN;
    return t->count++;
}

static void sub_track_free(sub_track_t *t) {
    size_t i;

    for (i = 0; i < t->count; ++i) {
        free(t->names[i]);
        free(t->spell[i]);
    }
    as_strmap_free(&t->by_name);
    free(t->names);
    free(t->spell);
    free(t->low);
    free(t->high);
    free(t->stack);
    memset(t, 0, sizeof(*t));
}

static int sub_track_start(sub_track_t *t) {
    t->stack_count = 0;
    t->at.cur.sec = sub_intern(t, ".text");
    t->at.cur.sub = 0;
    t->at.prev = t->at.cur;
    return t->at.cur.sec == (size_t)-1 ? -1 : 0;
}

/* The number of a subsection.  GNU as takes any, keeps 32 bits of it and
 * orders them as signed, so that -1 is before 0. */
static int sub_number(const char *raw, int *out) {
    long long v;

    if (raw == NULL || as_expr_eval_string(raw, NULL, NULL, &v) != AS_EXPR_EVAL_OK) {
        return -1;
    }
    *out = (int)(unsigned)(unsigned long long)v;
    return 0;
}

static int sub_has_arg(const as_directive_t *d, size_t i) {
    return d->arg_count > i && d->args[i] != NULL && d->args[i][0] != '\0';
}

/* Whether the second argument of a .pushsection is a subsection's number
 * and not a string of flags. */
static int sub_pushsection_has_number(const as_directive_t *d) {
    long long v;

    return d->arg_count == 2 && sub_has_arg(d, 1) && strchr(d->args[1], '"') == NULL &&
           as_expr_eval_string(d->args[1], NULL, NULL, &v) == AS_EXPR_EVAL_OK;
}

/*
 * Follow a directive that says where what comes next is assembled.  1 if
 * it is one, 0 if not, -1 for a number that is none or for no memory.
 */
static int sub_track_apply(sub_track_t *t, const as_directive_t *d) {
    sub_where_t next;

    if (d->name == NULL) {
        return 0;
    }
    if (strcmp(d->name, ".previous") == 0) {
        sub_where_t tmp = t->at.cur;

        t->at.cur = t->at.prev;
        t->at.prev = tmp;
        return 1;
    }
    if (strcmp(d->name, ".popsection") == 0) {
        if (t->stack_count != 0) {
            t->at = t->stack[--t->stack_count];
        }
        return 1;
    }
    if (strcmp(d->name, ".subsection") == 0) {
        next.sec = t->at.cur.sec;
        if (d->arg_count < 1 || sub_number(d->args[0], &next.sub) != 0) {
            return -1;
        }
    } else if (strcmp(d->name, ".text") == 0 || strcmp(d->name, ".data") == 0 || strcmp(d->name, ".bss") == 0 ||
               strcmp(d->name, ".rodata") == 0) {
        next.sec = sub_intern(t, d->name);
        next.sub = 0;
        if (next.sec == (size_t)-1 || (sub_has_arg(d, 0) && sub_number(d->args[0], &next.sub) != 0)) {
            return -1;
        }
    } else if (strcmp(d->name, ".section") == 0 || strcmp(d->name, ".pushsection") == 0) {
        int push = d->name[1] == 'p';

        if (!sub_has_arg(d, 0)) {
            return 0;
        }
        next.sec = sub_intern(t, d->args[0]);
        next.sub = 0;
        /* Not a negative one here: GNU as does not read it as a number. */
        if (next.sec == (size_t)-1 ||
            (push && sub_pushsection_has_number(d) && (sub_number(d->args[1], &next.sub) != 0 || next.sub < 0))) {
            return -1;
        }
        if (push) {
            if (t->stack_count == t->stack_cap) {
                size_t ncap = t->stack_cap == 0 ? 8 : t->stack_cap * 2;
                sub_frame_t *stack = (sub_frame_t *)realloc_array(t->stack, ncap, sizeof(*stack));

                if (stack == NULL) {
                    return -1;
                }
                t->stack = stack;
                t->stack_cap = ncap;
            }
            t->stack[t->stack_count++] = t->at;
        }
    } else {
        return 0;
    }
    t->at.prev = t->at.cur;
    t->at.cur = next;
    return 1;
}

static const char *sub_mode_directive(const as_stmt_t *st) {
    static const char *const modes[] = {".code16", ".code32", ".code64"};
    size_t i;

    if (st->kind != AS_STMT_DIRECTIVE || st->u.directive.name == NULL) {
        return NULL;
    }
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        if (strcmp(st->u.directive.name, modes[i]) == 0) {
            return modes[i];
        }
    }
    return NULL;
}

static int sub_vec_push(sub_stmt_vec_t *v, const as_stmt_t *st) {
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 64 : v->cap * 2;
        as_stmt_t *items = (as_stmt_t *)realloc_array(v->items, ncap, sizeof(*items));

        if (items == NULL) {
            return -1;
        }
        v->items = items;
        v->cap = ncap;
    }
    v->items[v->count++] = *st;
    return 0;
}

/* A directive of one argument or none, at the place of the statement
 * `at`, into `out`. */
static int sub_make_directive(as_stmt_t *out, const as_stmt_t *at, const char *name, const char *arg) {
    memset(out, 0, sizeof(*out));
    out->kind = AS_STMT_DIRECTIVE;
    out->line = at->line;
    out->file = at->file != NULL ? xstrdup(at->file) : NULL;
    out->u.directive.name = xstrdup(name);
    if ((at->file != NULL && out->file == NULL) || out->u.directive.name == NULL) {
        goto fail;
    }
    if (arg != NULL) {
        out->u.directive.args = (char **)calloc(1, sizeof(char *));
        if (out->u.directive.args == NULL) {
            goto fail;
        }
        out->u.directive.arg_cap = 1;
        out->u.directive.args[0] = xstrdup(arg);
        if (out->u.directive.args[0] == NULL) {
            goto fail;
        }
        out->u.directive.arg_count = 1;
    }
    return 0;
fail:
    free(out->file);
    free(out->u.directive.name);
    free(out->u.directive.args);
    memset(out, 0, sizeof(*out));
    return -1;
}

static void sub_drop_args(as_directive_t *d, size_t keep) {
    while (d->arg_count > keep) {
        free(d->args[--d->arg_count]);
        d->args[d->arg_count] = NULL;
    }
}

static int sub_moved_cmp(const void *a, const void *b) {
    const sub_moved_t *x = (const sub_moved_t *)a;
    const sub_moved_t *y = (const sub_moved_t *)b;

    if (x->where.sec != y->where.sec) {
        return x->where.sec < y->where.sec ? -1 : 1;
    }
    if (x->where.sub != y->where.sub) {
        return x->where.sub < y->where.sub ? -1 : 1;
    }
    return x->seq < y->seq ? -1 : (x->seq > y->seq ? 1 : 0);
}

int as_sections_gather_subsections(as_parse_result_t *parsed, unsigned default_code_bits,
                                   char *errbuf, size_t errbuf_sz) {
    sub_track_t t;
    sub_stmt_vec_t main_v;
    sub_moved_t *moved = NULL;
    size_t moved_count = 0;
    size_t moved_cap = 0;
    const char *mode = default_code_bits == 64 ? ".code64" : (default_code_bits == 16 ? ".code16" : ".code32");
    int any_numbered = 0;
    int any_mode = 0;
    int need_header = 1;
    size_t i;

    if (parsed == NULL) {
        return -1;
    }
    memset(&t, 0, sizeof(t));
    memset(&main_v, 0, sizeof(main_v));
    if (sub_track_start(&t) != 0) {
        goto oom;
    }
    t.low[t.at.cur.sec] = t.high[t.at.cur.sec] = 0;
    /* Most sources have none, and are left as they are. */
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];

        if (sub_mode_directive(st) != NULL) {
            any_mode = 1;
        }
        if (st->kind != AS_STMT_DIRECTIVE) {
            continue;
        }
        if (sub_track_apply(&t, &st->u.directive) < 0) {
            if (errbuf != NULL && errbuf_sz != 0) {
                snprintf(errbuf, errbuf_sz, "%s:%u: %s: not the number of a subsection",
                         st->file != NULL ? st->file : "<input>", st->line, st->u.directive.name);
            }
            sub_track_free(&t);
            return -1;
        }
        if (t.at.cur.sub < t.low[t.at.cur.sec]) {
            t.low[t.at.cur.sec] = t.at.cur.sub;
        }
        if (t.at.cur.sub > t.high[t.at.cur.sec]) {
            t.high[t.at.cur.sec] = t.at.cur.sub;
        }
    }
    for (i = 0; i < t.count; ++i) {
        /* One subsection alone, with a number, stays where it is and
         * has its number taken off. */
        if (t.low[i] < t.high[i] || t.high[i] != 0) {
            any_numbered = 1;
        }
    }
    if (!any_numbered) {
        sub_track_free(&t);
        return 0;
    }

/* The lowest subsection of a section is the one that stays. */
#define SUB_IS_MOVED(t) ((t).at.cur.sub != (t).low[(t).at.cur.sec])

#define SUB_MOVE(stmt)                                                                        \
    do {                                                                                       \
        if (moved_count == moved_cap) {                                                        \
            size_t ncap = moved_cap == 0 ? 64 : moved_cap * 2;                                 \
            sub_moved_t *next = (sub_moved_t *)realloc_array(moved, ncap, sizeof(*next));      \
            if (next == NULL) {                                                                \
                goto oom;                                                                      \
            }                                                                                  \
            moved = next;                                                                      \
            moved_cap = ncap;                                                                  \
        }                                                                                      \
        moved[moved_count].where = t.at.cur;                                                   \
        moved[moved_count].seq = moved_count;                                                  \
        moved[moved_count].st = (stmt);                                                        \
        moved_count++;                                                                         \
    } while (0)

    if (sub_track_start(&t) != 0) {
        goto oom;
    }
    for (i = 0; i < parsed->count; ++i) {
        parsed->items[i].source_seq = i + 1;
    }
    for (i = 0; i < parsed->count; ++i) {
        as_stmt_t *st = &parsed->items[i];
        as_directive_t *d = &st->u.directive;
        int control = 0;
        as_stmt_t made;

        if (st->kind == AS_STMT_DIRECTIVE && d->name != NULL &&
            (strcmp(d->name, ".previous") == 0 || strcmp(d->name, ".popsection") == 0 ||
             strcmp(d->name, ".subsection") == 0 || strcmp(d->name, ".text") == 0 || strcmp(d->name, ".data") == 0 ||
             strcmp(d->name, ".bss") == 0 || strcmp(d->name, ".rodata") == 0 ||
             ((strcmp(d->name, ".section") == 0 || strcmp(d->name, ".pushsection") == 0) && sub_has_arg(d, 0)))) {
            control = 1;
        }
        if (!control && !SUB_IS_MOVED(t)) {
            if (sub_mode_directive(st) != NULL) {
                mode = sub_mode_directive(st);
            }
            if (sub_vec_push(&main_v, st) != 0) {
                goto oom;
            }
            continue;
        }
        if (SUB_IS_MOVED(t) && (!control || st->label_count != 0)) {
            /* A run of a numbered subsection opens with the section it
             * is of and the mode it was written in. */
            if (need_header) {
                if (sub_make_directive(&made, st, ".section", t.spell[t.at.cur.sec]) != 0) {
                    goto oom;
                }
                SUB_MOVE(made);
                if (any_mode) {
                    if (sub_make_directive(&made, st, mode, NULL) != 0) {
                        goto oom;
                    }
                    SUB_MOVE(made);
                }
                need_header = 0;
            }
        }
        if (!control) {
            /* A change of mode is for all that follows it in the source,
             * here and where the rest of the statements stay. */
            if (sub_mode_directive(st) != NULL) {
                mode = sub_mode_directive(st);
                if (sub_make_directive(&made, st, mode, NULL) != 0 || sub_vec_push(&main_v, &made) != 0) {
                    goto oom;
                }
            }
            SUB_MOVE(*st);
            continue;
        }
        /* The labels on a directive that leaves a numbered subsection
         * are in the subsection it leaves. */
        if (SUB_IS_MOVED(t) && st->label_count != 0) {
            memset(&made, 0, sizeof(made));
            made.kind = AS_STMT_LABEL_ONLY;
            made.source_seq = st->source_seq;
            made.line = st->line;
            made.file = st->file != NULL ? xstrdup(st->file) : NULL;
            if (st->file != NULL && made.file == NULL) {
                goto oom;
            }
            made.labels = st->labels;
            made.label_count = st->label_count;
            made.label_cap = st->label_cap;
            st->labels = NULL;
            st->label_count = 0;
            st->label_cap = 0;
            SUB_MOVE(made);
        }
        if (sub_track_apply(&t, d) < 0) {
            goto oom;
        }
        need_header = 1;
        /* The directive stays, without its number: what is left of it
         * is the change of section, which is all the rest know of. */
        if (strcmp(d->name, ".subsection") == 0) {
            char *name = xstrdup(".section");
            char *arg = xstrdup(t.spell[t.at.cur.sec]);

            if (name == NULL || arg == NULL || d->arg_count < 1) {
                free(name);
                free(arg);
                goto oom;
            }
            sub_drop_args(d, 1);
            free(d->args[0]);
            d->args[0] = arg;
            free(d->name);
            d->name = name;
        } else if (strcmp(d->name, ".pushsection") == 0) {
            if (sub_pushsection_has_number(d)) {
                sub_drop_args(d, 1);
            }
        } else if (strcmp(d->name, ".section") != 0) {
            sub_drop_args(d, 0);
        }
        if (sub_vec_push(&main_v, st) != 0) {
            goto oom;
        }
    }
#undef SUB_MOVE
#undef SUB_IS_MOVED

    qsort(moved, moved_count, sizeof(*moved), sub_moved_cmp);
    for (i = 0; i < moved_count; ++i) {
        if (sub_vec_push(&main_v, &moved[i].st) != 0) {
            goto oom;
        }
    }
    free(moved);
    free(parsed->items);
    /* (The room that doubling made and is not used is given back, as
     * the parser gives its own back: these are kept to the end.) */
    if (main_v.count != 0 && main_v.count < main_v.cap) {
        as_stmt_t *fit = (as_stmt_t *)realloc(main_v.items, main_v.count * sizeof(*fit));

        if (fit != NULL) {
            main_v.items = fit;
            main_v.cap = main_v.count;
        }
    }
    parsed->items = main_v.items;
    parsed->count = main_v.count;
    parsed->cap = main_v.cap;
    sub_track_free(&t);
    return 0;

oom:
    /* Statements are in two places or in neither by now; the assembler
     * is about to exit, and is not to free any of them twice. */
    parsed->count = 0;
    free(moved);
    free(main_v.items);
    sub_track_free(&t);
    if (errbuf != NULL && errbuf_sz != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory gathering subsections");
    }
    return -1;
}

int as_sections_build(const as_parse_result_t *parsed, as_section_state_t *out,
                      char *errbuf, size_t errbuf_sz) {
    sec_ctx_t ctx;
    size_t i;
    int rc = 0;

    if (parsed == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (ensure_builtins(out) != 0 || section_track_init(&ctx.track, 32u) != 0) {
        set_err(&ctx, "section state init failed");
        return -1;
    }

    for (i = 0; i < parsed->count && rc == 0; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        if (st->kind != AS_STMT_DIRECTIVE) {
            continue;
        }
        switch (process_directive(&ctx, st)) {
        case 0:
            break;
        case SEC_SAID:
            rc = -1;
            break;
        case SEC_NO_SUBSECTIONS:
            set_err(&ctx, "%s:%u: %s: subsections other than 0 are not supported",
                    st->file, st->line, st->u.directive.name);
            rc = -1;
            break;
        default:
            set_err(&ctx, "%s:%u: malformed section directive %s", st->file, st->line, st->u.directive.name);
            rc = -1;
            break;
        }
    }

    section_track_free(&ctx.track);
    return rc;
}
