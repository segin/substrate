#ifndef SUBSTRATE_REGEX_H
#define SUBSTRATE_REGEX_H

#include <stddef.h>
#include <sys/types.h>

#include <regex/flags.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque objects */
typedef struct regex_t regex_t;             /* compiled pattern */
typedef struct regex_iter_t regex_iter_t;   /* streaming/iterator state */

/* Library limits for defensive defaults */
typedef struct regex_limits_t {
    size_t max_states;        /* maximum compiled NFA/DFA states */
    size_t max_captures;      /* maximum capture groups */
    size_t match_steps;       /* maximum VM steps per match attempt */
    size_t max_matches;       /* maximum matches returned in find/iter */
    size_t max_stream_buffer; /* maximum buffered bytes for streaming */
} regex_limits_t;

/* Return default limits for the safe engine */
regex_limits_t regex_default_limits(void);

/* compile: returns NULL on error; out_err receives code */
regex_t *regex_compile(const char *pattern, unsigned flags, regex_err_t *out_err);

/* update limits (NULL to reset to defaults) */
regex_err_t regex_set_limits(regex_t *re, const regex_limits_t *limits);

/* free */
void regex_free(regex_t *re);

/* capture count */
size_t regex_capture_count(const regex_t *re);

/* Basic match APIs: return number of captures on success (>=0), -1 on no match, or negative error */
ssize_t regex_match(const regex_t *re, const char *text, size_t text_len,
                    size_t *capture_offsets /* preallocated array of 2*capcount */,
                    size_t max_captures, regex_err_t *out_err);

/* Find all matches: allocate/append to a caller-provided callback */
typedef int (*regex_match_cb)(void *user, size_t start, size_t end,
                              const size_t *capture_offsets, size_t capture_count);
regex_err_t regex_find_all(const regex_t *re, const char *text, size_t text_len,
                           regex_match_cb cb, void *user, size_t max_matches);

/* replace: single or global; returns newly allocated string via out_len (caller frees) */
regex_err_t regex_replace(const regex_t *re, const char *text, size_t text_len,
                          const char *replacement, int global,
                          char **out_buf, size_t *out_len);

/* split: return array of segments */
typedef struct regex_split_result_t {
    char **items;
    size_t count;
} regex_split_result_t;

regex_err_t regex_split(const regex_t *re, const char *text, size_t text_len,
                        regex_split_result_t *out, size_t max_splits);
void regex_split_free(regex_split_result_t *result);

/* Streaming API */
regex_iter_t *regex_iter_create(const regex_t *re, unsigned options, regex_err_t *out_err);
regex_err_t regex_iter_feed(regex_iter_t *it, const char *chunk, size_t len);
regex_err_t regex_iter_finish(regex_iter_t *it); /* signal EOF; returns any final matches */
ssize_t regex_iter_next(regex_iter_t *it, size_t *start, size_t *end,
                        size_t *capture_offsets, size_t max_captures,
                        size_t *out_cap_count);
regex_err_t regex_iter_last_error(const regex_iter_t *it);
void regex_iter_destroy(regex_iter_t *it);

/* utility: escape literal for insertion into patterns */
char *regex_escape_literal(const char *s, size_t len); /* returns malloc'ed string */

#ifdef __cplusplus
}
#endif

#endif /* SUBSTRATE_REGEX_H */
