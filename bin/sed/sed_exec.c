/*
 * sed_exec.c - command execution engine for the sed stream editor.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sed.h"

/* ------------------------------------------------------------------ */
/* l command: visual (unambiguous) output                               */
/* ------------------------------------------------------------------ */

void
sed_list_print(const char *s, size_t len, int wrap, FILE *out)
{
    if (wrap <= 0) wrap = DEFAULT_LIST_WRAP;
    int col = 0;

#define EMIT(str, n) do { \
    int _n = (n); \
    if (wrap > 1 && col + _n >= wrap) { fputs("\\\n", out); col = 0; } \
    fwrite((str), 1, (size_t)_n, out); col += _n; \
} while (0)

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        char esc[8]; int el;
        switch (c) {
        case '\\': el = 2; memcpy(esc, "\\\\", 2); break;
        case '\a': el = 2; memcpy(esc, "\\a",  2); break;
        case '\b': el = 2; memcpy(esc, "\\b",  2); break;
        case '\f': el = 2; memcpy(esc, "\\f",  2); break;
        case '\n': el = 2; memcpy(esc, "\\n",  2); break;
        case '\r': el = 2; memcpy(esc, "\\r",  2); break;
        case '\t': el = 2; memcpy(esc, "\\t",  2); break;
        case '\v': el = 2; memcpy(esc, "\\v",  2); break;
        default:
            if (isprint(c)) { esc[0]=(char)c; el=1; }
            else { el = snprintf(esc,sizeof(esc),"\\%03o",c); }
            break;
        }
        EMIT(esc, el);
    }
    EMIT("$", 1);
    fputc('\n', out);
#undef EMIT
}

/* ------------------------------------------------------------------ */
/* Substitution                                                          */
/* ------------------------------------------------------------------ */

static void
emit_repl(const char *text, size_t tlen,
          const char *repl,
          const size_t *caps, size_t ncaps_pairs,
          dynbuf_t *out)
{
    int cvmode = 0; /* 0=none 1=\u 2=\l 3=\U 4=\L */

    for (const char *rp = repl; *rp; rp++) {
        if (*rp == '&') {
            size_t ms = caps[0], me = caps[1];
            for (size_t k = ms; k < me && k < tlen; k++) {
                char ch = text[k];
                if      (cvmode==1){ch=(char)toupper((unsigned char)ch);cvmode=0;}
                else if (cvmode==2){ch=(char)tolower((unsigned char)ch);cvmode=0;}
                else if (cvmode==3) ch=(char)toupper((unsigned char)ch);
                else if (cvmode==4) ch=(char)tolower((unsigned char)ch);
                db_appendc(out, ch);
            }
            continue;
        }
        if (*rp != '\\') {
            char ch = *rp;
            if      (cvmode==1){ch=(char)toupper((unsigned char)ch);cvmode=0;}
            else if (cvmode==2){ch=(char)tolower((unsigned char)ch);cvmode=0;}
            else if (cvmode==3) ch=(char)toupper((unsigned char)ch);
            else if (cvmode==4) ch=(char)tolower((unsigned char)ch);
            db_appendc(out, ch); continue;
        }
        rp++;
        if (!*rp) break;
        if (*rp >= '1' && *rp <= '9') {
            int g = *rp - '0';
            if (g * 2 + 1 < (int)ncaps_pairs &&
                caps[g*2] != (size_t)-1) {
                size_t gs = caps[g*2], ge = caps[g*2+1];
                for (size_t k = gs; k < ge && k < tlen; k++) {
                    char ch = text[k];
                    if      (cvmode==1){ch=(char)toupper((unsigned char)ch);cvmode=0;}
                    else if (cvmode==2){ch=(char)tolower((unsigned char)ch);cvmode=0;}
                    else if (cvmode==3) ch=(char)toupper((unsigned char)ch);
                    else if (cvmode==4) ch=(char)tolower((unsigned char)ch);
                    db_appendc(out, ch);
                }
            }
        } else if (*rp == '&')  { db_appendc(out, '&'); }
        else if (*rp == 'n')    { db_appendc(out, '\n'); }
        else if (*rp == 't')    { db_appendc(out, '\t'); }
        else if (*rp == '\\')   { db_appendc(out, '\\'); }
        else if (*rp == 'u')    { cvmode = 1; }
        else if (*rp == 'l')    { cvmode = 2; }
        else if (*rp == 'U')    { cvmode = 3; }
        else if (*rp == 'L')    { cvmode = 4; }
        else if (*rp == 'E')    { cvmode = 0; }
        else { db_appendc(out, '\\'); db_appendc(out, *rp); }
    }
}

int
sed_do_subst(subst_t *sub, dynbuf_t *pat)
{
    if (!sub->pat) return 0;

    const char *text = pat->buf ? pat->buf : "";
    size_t      tlen = pat->len;

    /* Compile fresh regex for each substitution (workaround for
     * regex_t statefulness bug: object is broken after first use). */
    regex_err_t compile_err;
    regex_t *re = regex_compile(sub->pat, sub->reflags, &compile_err);
    if (!re) return 0;

    size_t ncaps = regex_capture_count(re);
    if (ncaps < 1) ncaps = 1;
    size_t *caps = malloc(ncaps * 2 * sizeof(size_t));
    if (!caps) { regex_free(re); die("out of memory"); }

    dynbuf_t result; db_init(&result);
    size_t pos = 0;
    int matched = 0;
    int occur   = 0;

    while (pos <= tlen) {
        /* Must compile fresh each iteration: regex_t is stateful and breaks
         * after first use (library bug). */
        regex_free(re);
        re = regex_compile(sub->pat, sub->reflags, &compile_err);
        if (!re) break;
        regex_err_t err;
        ssize_t rc = regex_match(re, text + pos, tlen - pos,
                                 caps, ncaps * 2, &err);
        if ((int)rc < 0) break;

        /* caps are relative to pos; make absolute */
        size_t mstart = caps[0] + pos;
        size_t mend   = caps[1] + pos;
        for (size_t i = 0; i < ncaps; i++) {
            if (caps[i*2] != (size_t)-1) {
                caps[i*2]   += pos;
                caps[i*2+1] += pos;
            }
        }

        occur++;
        bool do_repl = (sub->nth == 0) || (occur == sub->nth);

        if (!do_repl) {
            /* not our target occurrence: copy through including match */
            db_append(&result, text + pos, mend - pos);
            pos = mend;
            if (mend == mstart) {
                if (pos < tlen) db_appendc(&result, text[pos++]);
                else break;
            }
            continue;
        }

        /* copy text before match */
        db_append(&result, text + pos, mstart - pos);
        /* emit replacement */
        emit_repl(text, tlen, sub->repl, caps, ncaps * 2, &result);
        matched = 1;
        pos = mend;

        if (!sub->global || (sub->nth != 0)) break;

        /* avoid infinite loop on zero-length match */
        if (mend == mstart) {
            if (pos < tlen) db_appendc(&result, text[pos++]);
            else break;
        }
    }

    if (re) regex_free(re);

    if (!matched) {
        free(caps); db_free(&result);
        return 0;
    }

    /* copy tail */
    if (pos <= tlen) db_append(&result, text + pos, tlen - pos);
    db_ensure_nul(&result);
    db_free(pat);
    *pat = result;
    free(caps);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Line reader                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    FILE    *fp;
    dynbuf_t cur;
    dynbuf_t nxt;
    bool     have_nxt;
    bool     eof;
    bool     null_delim;
} lr_t;

static void
lr_init(lr_t *lr, FILE *fp, bool nd)
{
    lr->fp = fp; lr->null_delim = nd;
    lr->eof = false; lr->have_nxt = false;
    db_init(&lr->cur); db_init(&lr->nxt);
}

static void lr_free(lr_t *lr) { db_free(&lr->cur); db_free(&lr->nxt); }

static bool
lr_read1(lr_t *lr, dynbuf_t *db)
{
    db_clear(db);
    int c;
    while ((c = fgetc(lr->fp)) != EOF) {
        db_appendc(db, (char)c);
        if (lr->null_delim ? c == '\0' : c == '\n') break;
    }
    return db->len > 0;
}

static void
lr_prefetch(lr_t *lr)
{
    if (!lr->have_nxt && !lr->eof) {
        if (!lr_read1(lr, &lr->nxt)) lr->eof = true;
        else lr->have_nxt = true;
    }
}

/* advance: move nxt into cur. Returns false when there's nothing more. */
static bool
lr_advance(lr_t *lr)
{
    lr_prefetch(lr);
    if (!lr->have_nxt) return false;
    dynbuf_t tmp = lr->cur; lr->cur = lr->nxt; lr->nxt = tmp;
    lr->have_nxt = false;
    lr_prefetch(lr); /* prime nxt for is_last check */
    return true;
}

static bool
lr_is_last(lr_t *lr) { lr_prefetch(lr); return !lr->have_nxt; }

/* ------------------------------------------------------------------ */
/* Address matching                                                      */
/* ------------------------------------------------------------------ */

/* Compile-match-free helper: workaround for regex_t statefulness bug */
static int
pat_match(const char *pat, unsigned reflags,
          const char *text, size_t tlen,
          size_t *caps, size_t ncaps)
{
    if (!pat) return -1;
    regex_err_t err;
    regex_t *re = regex_compile(pat, reflags, &err);
    if (!re) return -1;
    int rc = (int)regex_match(re, text, tlen, caps, ncaps, &err);
    regex_free(re);
    return rc;
}

static bool
match_one_addr(const addr_t *a)
{
    const char *text = G.pat.buf ? G.pat.buf : "";
    size_t      tlen = G.pat.len;

    switch (a->type) {
    case A_NONE: return true;
    case A_LINE: return G.lineno == a->line;
    case A_LAST: return G.last_line;
    case A_STEP:
        if (a->step == 0) return G.lineno == a->line;
        if (a->line == 0) return (G.lineno % a->step) == 0;
        return G.lineno >= a->line &&
               ((G.lineno - a->line) % a->step) == 0;
    case A_REGEX:
        if (!a->pat) return false;
        return pat_match(a->pat, a->reflags, text, tlen, NULL, 0) >= 0;
    default: return false;
    }
}

static bool
addr_matches(cmd_t *c)
{
    if (c->naddr == 0) return !c->negate;

    if (c->naddr == 1) {
        bool m = match_one_addr(&c->addr[0]);
        return c->negate ? !m : m;
    }

    /* 2-address range */
    addr_t *a1 = &c->addr[0], *a2 = &c->addr[1];

    if (!c->in_range) {
        /* check if addr1 matches (0-addr means: enter at any line) */
        bool enter;
        if (a1->type == A_LINE && a1->line == 0) {
            /* addr=0 is only valid as part of 0,/re/ - always enters */
            enter = true;
        } else {
            enter = match_one_addr(a1);
        }
        if (!enter) return c->negate;

        c->in_range = 1;
        /* precompute end for relative addresses */
        if (a2->type == A_RELOFF) {
            c->range_end = G.lineno + a2->line;
        } else if (a2->type == A_RELMUL) {
            long n = a2->line > 0 ? a2->line : 1;
            c->range_end = ((G.lineno + n - 1) / n) * n;
        }

        /* check immediate exit (single-line range) */
        bool done = false;
        switch (a2->type) {
        case A_LINE:    done = G.lineno >= a2->line; break;
        case A_LAST:    done = G.last_line; break;
        case A_RELOFF:
        case A_RELMUL:  done = G.lineno >= c->range_end; break;
        case A_REGEX:
            /* 0,/re/: regex can match on line 1 */
            if (a1->type == A_LINE && a1->line == 0)
                done = pat_match(a2->pat, a2->reflags,
                                 G.pat.buf ? G.pat.buf : "",
                                 G.pat.len, NULL, 0) >= 0;
            else
                done = false; /* re match checked on NEXT lines */
            break;
        default: break;
        }
        if (done) c->in_range = 0;
        return !c->negate;
    }

    /* in_range=1: check exit condition */
    bool done = false;
    switch (a2->type) {
    case A_LINE:    done = G.lineno >= a2->line; break;
    case A_LAST:    done = G.last_line; break;
    case A_RELOFF:
    case A_RELMUL:  done = G.lineno >= c->range_end; break;
    case A_REGEX:
        done = pat_match(a2->pat, a2->reflags,
                         G.pat.buf ? G.pat.buf : "",
                         G.pat.len, NULL, 0) >= 0;
        break;
    default: break;
    }
    if (done) c->in_range = 0;
    return !c->negate;
}

/* ------------------------------------------------------------------ */
/* Output helpers                                                        */
/* ------------------------------------------------------------------ */

static FILE *cur_out;
static lr_t *cur_lr;

static void
flush_appends(void)
{
    if (G.append_queue.len > 0) {
        fwrite(G.append_queue.buf, 1, G.append_queue.len, cur_out);
        db_clear(&G.append_queue);
    }
}

static void
print_pat(void)
{
    if (G.pat.len > 0) fwrite(G.pat.buf, 1, G.pat.len, cur_out);
    if (G.pat.len == 0 || G.pat.buf[G.pat.len-1] != '\n')
        fputc('\n', cur_out);
}

static void
print_pat_to(FILE *fp)
{
    if (G.pat.len > 0) fwrite(G.pat.buf, 1, G.pat.len, fp);
    if (G.pat.len == 0 || G.pat.buf[G.pat.len-1] != '\n')
        fputc('\n', fp);
}

static void
print_first_line_to(FILE *fp)
{
    const char *ps = G.pat.buf ? G.pat.buf : "";
    const char *nl = memchr(ps, '\n', G.pat.len);
    size_t n = nl ? (size_t)(nl - ps + 1) : G.pat.len;
    fwrite(ps, 1, n, fp);
    if (!nl) fputc('\n', fp);
}

/* ------------------------------------------------------------------ */
/* Internal return codes from run_cmds                                  */
/* ------------------------------------------------------------------ */

#define RC_CONTINUE  0
#define RC_NEWCYCLE  1  /* start new cycle (d) */
#define RC_RESTART   2  /* restart without reading (D) */
#define RC_QUIT      3  /* q / Q */

/* ------------------------------------------------------------------ */
/* Command execution loop                                               */
/* ------------------------------------------------------------------ */

static int
run_cmds(cmd_t *start)
{
    cmd_t *c = start;
    while (c) {
        bool match = addr_matches(c);
        if (!match) {
            if (c->type == C_LBRACE && c->end_block)
                c = c->end_block->next;
            else
                c = c->next;
            continue;
        }

        switch (c->type) {

        case C_LABEL: case C_RBRACE:
            /* no-op */
            break;

        case C_LBRACE:
            /* matched: enter block normally */
            break;

        /* ---- output ---- */

        case C_PRINT:
            print_pat();
            break;

        case C_PRINTP: {
            const char *ps = G.pat.buf ? G.pat.buf : "";
            const char *nl = memchr(ps, '\n', G.pat.len);
            size_t n = nl ? (size_t)(nl - ps + 1) : G.pat.len;
            fwrite(ps, 1, n, cur_out);
            if (!nl) fputc('\n', cur_out);
            break;
        }

        case C_EQUAL:
            fprintf(cur_out, "%ld\n", G.lineno);
            break;

        case C_FILE:
            fprintf(cur_out, "%s\n",
                    G.cur_filename ? G.cur_filename : "");
            break;

        case C_LIST:
            sed_list_print(G.pat.buf ? G.pat.buf : "", G.pat.len,
                           c->num > 0 ? c->num : G.list_wrap, cur_out);
            break;

        /* ---- text insertion ---- */

        case C_APPEND:
            db_append(&G.append_queue, c->text, strlen(c->text));
            break;

        case C_INSERT:
            fputs(c->text, cur_out);
            break;

        case C_CHANGE: {
            db_clear(&G.pat);
            /* output text only when NOT still in range */
            if (!c->in_range)
                fputs(c->text, cur_out);
            G.no_print = true;
            return RC_NEWCYCLE;
        }

        /* ---- delete ---- */

        case C_DELETE:
            db_clear(&G.pat);
            G.no_print = true;
            return RC_NEWCYCLE;

        case C_DELETEP: {
            char *ps = G.pat.buf;
            if (ps) {
                char *nl = memchr(ps, '\n', G.pat.len);
                if (nl) {
                    size_t off = (size_t)(nl - ps) + 1;
                    memmove(ps, ps + off, G.pat.len - off);
                    G.pat.len -= off;
                    if (G.pat.buf) G.pat.buf[G.pat.len] = '\0';
                } else {
                    db_clear(&G.pat);
                }
            }
            G.subst_flag = false;
            return RC_RESTART;
        }

        /* ---- next line ---- */

        case C_NEXT:
            if (!G.suppress) print_pat();
            flush_appends();
            G.no_print = false;
            G.subst_flag = false;
            if (G.last_line) return RC_QUIT;
            if (!lr_advance(cur_lr)) return RC_QUIT;
            {
                dynbuf_t *ln = &cur_lr->cur;
                size_t raw = ln->len;
                bool had_nl = raw > 0 && ln->buf[raw-1] == '\n';
                db_set(&G.pat, ln->buf, had_nl ? raw-1 : raw);
                db_ensure_nul(&G.pat);
                G.lineno++;
                G.last_line = lr_is_last(cur_lr);
            }
            break;

        case C_NEXTAPP: {
            /* N appends next line to pattern space with embedded \n; no output */
            if (G.last_line) {
                /* N at last line: write pattern space and exit */
                if (!G.suppress) print_pat();
                flush_appends();
                G.no_print = true;
                return RC_QUIT;
            }
            if (!lr_advance(cur_lr)) {
                if (!G.suppress) print_pat();
                flush_appends();
                G.no_print = true;
                return RC_QUIT;
            }
            dynbuf_t *ln = &cur_lr->cur;
            size_t raw = ln->len;
            bool had_nl = raw > 0 && ln->buf[raw-1] == '\n';
            db_appendc(&G.pat, '\n');
            db_append(&G.pat, ln->buf, had_nl ? raw-1 : raw);
            db_ensure_nul(&G.pat);
            G.lineno++;
            G.last_line = lr_is_last(cur_lr);
            break;
        }

        /* ---- hold space ---- */

        case C_HOLD:
            db_set(&G.hold, G.pat.buf ? G.pat.buf : "", G.pat.len);
            break;
        case C_HOLDAPP:
            db_appendc(&G.hold, '\n');
            db_append(&G.hold, G.pat.buf ? G.pat.buf : "", G.pat.len);
            break;
        case C_GET:
            db_set(&G.pat, G.hold.buf ? G.hold.buf : "", G.hold.len);
            db_ensure_nul(&G.pat);
            break;
        case C_GETAPP:
            db_appendc(&G.pat, '\n');
            db_append(&G.pat, G.hold.buf ? G.hold.buf : "", G.hold.len);
            db_ensure_nul(&G.pat);
            break;
        case C_EXCH: {
            dynbuf_t tmp = G.pat; G.pat = G.hold; G.hold = tmp;
            db_ensure_nul(&G.pat);
            break;
        }

        /* ---- substitution ---- */

        case C_SUBST: {
            int r = sed_do_subst(c->subst, &G.pat);
            if (r > 0) {
                G.subst_flag = true;
                if (c->subst->print)  print_pat();
                if (c->subst->wfile >= 0)
                    print_pat_to(G.write_fps[c->subst->wfile]);
            }
            break;
        }

        /* ---- transliterate ---- */

        case C_TRANS:
            if (G.pat.buf) {
                for (size_t i = 0; i < G.pat.len; i++)
                    G.pat.buf[i] = (char)c->trans->map[
                        (unsigned char)G.pat.buf[i]];
            }
            break;

        /* ---- branch ---- */

        case C_BRANCH:
            if (!c->target) return RC_CONTINUE; /* b = end of script */
            c = c->target;
            continue;

        case C_BRANT:
            if (G.subst_flag) {
                G.subst_flag = false;
                if (!c->target) return RC_CONTINUE;
                c = c->target; continue;
            }
            break;

        case C_BRANTF:
            if (!G.subst_flag) {
                if (!c->target) return RC_CONTINUE;
                c = c->target; continue;
            }
            G.subst_flag = false;
            break;

        /* ---- quit ---- */

        case C_QUIT:
            if (!G.suppress) print_pat();
            flush_appends();
            G.exit_code = (c->num >= 0) ? c->num : 0;
            G.no_print  = true;
            return RC_QUIT;

        case C_QUITND:
            flush_appends();
            G.exit_code = (c->num >= 0) ? c->num : 0;
            G.no_print  = true;
            return RC_QUIT;

        /* ---- file I/O ---- */

        case C_READ:
            if (!G.sandbox) {
                FILE *rf = fopen(c->text, "r");
                if (rf) {
                    char rbuf[4096]; size_t rn;
                    while ((rn = fread(rbuf,1,sizeof(rbuf),rf)) > 0)
                        db_append(&G.append_queue, rbuf, rn);
                    fclose(rf);
                }
            }
            break;

        case C_READLN:
            if (!G.sandbox) {
                FILE *rf = fopen(c->text, "r");
                if (rf) {
                    char rbuf[4096];
                    if (fgets(rbuf, sizeof(rbuf), rf))
                        db_append(&G.append_queue, rbuf, strlen(rbuf));
                    fclose(rf);
                }
            }
            break;

        case C_WRITE:
            if (!G.sandbox && c->num >= 0)
                print_pat_to(G.write_fps[c->num]);
            break;

        case C_WRITELN:
            if (!G.sandbox && c->num >= 0)
                print_first_line_to(G.write_fps[c->num]);
            break;

        /* ---- misc ---- */

        case C_ZAP:
            db_clear(&G.pat);
            break;

        case C_EXEC:
            if (!G.sandbox) {
                if (c->text) {
                    (void)system(c->text);
                } else {
                    db_ensure_nul(&G.pat);
                    (void)system(G.pat.buf ? G.pat.buf : "");
                    db_clear(&G.pat);
                }
            }
            break;

        default:
            break;
        }

        c = c->next;
    }
    return RC_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Reset per-file state                                                  */
/* ------------------------------------------------------------------ */

static void
reset_for_file(void)
{
    for (cmd_t *c = G.cmds; c; c = c->next) {
        c->in_range  = 0;
        c->range_end = 0;
    }
    G.lineno     = 0;
    G.subst_flag = false;
}

/* ------------------------------------------------------------------ */
/* Process one file                                                      */
/* ------------------------------------------------------------------ */

int
sed_process_file(FILE *fp, const char *name, bool is_last_file)
{
    (void)is_last_file;

    cur_out = stdout;
    G.cur_filename = name;
    if (G.separate) reset_for_file();

    lr_t lr;
    lr_init(&lr, fp, G.null_delim);
    cur_lr = &lr;

    while (lr_advance(&lr)) {
        dynbuf_t *ln = &lr.cur;
        size_t raw = ln->len;
        bool had_nl = raw > 0 && ln->buf[raw-1] == '\n';

        db_set(&G.pat, ln->buf, had_nl ? raw-1 : raw);
        db_ensure_nul(&G.pat);
        G.lineno++;
        G.last_line  = lr_is_last(&lr);
        G.no_print   = false;
        G.subst_flag = false;

    restart:;
        int ret = run_cmds(G.cmds);

        if (ret == RC_RESTART) goto restart;

        if (ret == RC_QUIT) {
            lr_free(&lr);
            return G.exit_code;
        }

        if (!G.no_print && !G.suppress)
            print_pat();
        flush_appends();
    }

    lr_free(&lr);
    return 0;
}
