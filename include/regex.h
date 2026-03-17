#ifndef SUBSTRATE_REGEX_H
#define SUBSTRATE_REGEX_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <regex/flags.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration for opaque vtable */
struct regex_engine_vtable;

/* regex_limits_t must be defined before regex_t */
typedef struct regex_limits_t {
    size_t max_states;
    size_t max_captures;
    size_t match_steps;
    size_t max_matches;
    size_t max_stream_buffer;
} regex_limits_t;

/*
 * Concrete regex_t — provides both the substrate internal API
 * and the POSIX-compatible re_nsub field.
 */
typedef struct regex_t {
    size_t re_nsub;                           /* POSIX: parenthesized sub-count */
    unsigned flags;                           /* substrate compile flags */
    size_t capture_count;                     /* total capture groups (incl. group 0) */
    regex_limits_t limits;                    /* resource limits */
    const struct regex_engine_vtable *engine; /* opaque vtable */
    void *impl;                               /* engine-specific data */
} regex_t;

/* Opaque iterator */
typedef struct regex_iter_t regex_iter_t;

/* -----------------------------------------------------------------------
 * Substrate extended API
 * --------------------------------------------------------------------- */

regex_limits_t regex_default_limits(void);

regex_t *regex_compile(const char *pattern, unsigned flags, regex_err_t *out_err);
regex_err_t regex_set_limits(regex_t *re, const regex_limits_t *limits);
void regex_free(regex_t *re);
size_t regex_capture_count(const regex_t *re);

typedef int (*regex_match_cb)(void *user, size_t start, size_t end,
                              const size_t *capture_offsets, size_t capture_count);

ssize_t regex_match(const regex_t *re, const char *text, size_t text_len,
                    size_t *capture_offsets, size_t max_captures,
                    regex_err_t *out_err);
regex_err_t regex_find_all(const regex_t *re, const char *text, size_t text_len,
                           regex_match_cb cb, void *user, size_t max_matches);
regex_err_t regex_replace(const regex_t *re, const char *text, size_t text_len,
                          const char *replacement, int global,
                          char **out_buf, size_t *out_len);

typedef struct regex_split_result_t {
    char **items;
    size_t count;
} regex_split_result_t;

regex_err_t regex_split(const regex_t *re, const char *text, size_t text_len,
                        regex_split_result_t *out, size_t max_splits);
void regex_split_free(regex_split_result_t *result);

regex_iter_t *regex_iter_create(const regex_t *re, unsigned options,
                                regex_err_t *out_err);
regex_err_t regex_iter_feed(regex_iter_t *it, const char *chunk, size_t len);
regex_err_t regex_iter_finish(regex_iter_t *it);
ssize_t regex_iter_next(regex_iter_t *it, size_t *start, size_t *end,
                        size_t *capture_offsets, size_t max_captures,
                        size_t *out_cap_count);
regex_err_t regex_iter_last_error(const regex_iter_t *it);
void regex_iter_destroy(regex_iter_t *it);

char *regex_escape_literal(const char *s, size_t len);

/* -----------------------------------------------------------------------
 * POSIX compatibility layer
 * --------------------------------------------------------------------- */

/* Subexpression match */
typedef struct {
    int rm_so;   /* start of match */
    int rm_eo;   /* end of match */
} regmatch_t;

/* Compilation flags */
#define REG_EXTENDED  0x0001
#define REG_ICASE     0x0002
#define REG_NOSUB     0x0004
#define REG_NEWLINE   0x0008

/* Execution flags */
#define REG_NOTBOL    0x0010
#define REG_NOTEOL    0x0020

/* Error codes */
#define REG_NOMATCH   1
#define REG_BADPAT    2
#define REG_ECOLLATE  3
#define REG_ECTYPE    4
#define REG_EESCAPE   5
#define REG_ESUBREG   6
#define REG_EBRACK    7
#define REG_EPAREN    8
#define REG_EBRACE    9
#define REG_BADBR     10
#define REG_ERANGE    11
#define REG_ESPACE    12
#define REG_BADRPT    13

int  regcomp(regex_t *restrict preg, const char *restrict pattern, int cflags);
int  regexec(const regex_t *restrict preg, const char *restrict string,
             size_t nmatch, regmatch_t pmatch[restrict], int eflags);
void regfree(regex_t *preg);
size_t regerror(int errcode, const regex_t *restrict preg,
                char *restrict errbuf, size_t errbuf_size);

#ifdef __cplusplus
}
#endif

#endif /* SUBSTRATE_REGEX_H */
