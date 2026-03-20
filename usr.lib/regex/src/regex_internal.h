#ifndef SUBSTRATE_REGEX_INTERNAL_H
#define SUBSTRATE_REGEX_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <regex.h>

#define REGEX_MAX_CODEPOINT 0x10FFFFu

/* regex_t struct body is now defined in the public header */
/* regex_engine_vtable is forward-declared in public header; define it here */
typedef struct regex_engine_vtable regex_engine_vtable;

typedef struct regex_iter_t {
    const regex_engine_vtable *engine;
} regex_iter_t;

struct regex_engine_vtable {
    const char *name;
    regex_err_t (*compile)(regex_t *re, const char *pattern, unsigned flags);
    void (*destroy)(regex_t *re);
    ssize_t (*match)(const regex_t *re, const char *text, size_t text_len,
                     size_t *capture_offsets, size_t max_captures,
                     regex_err_t *out_err);
    regex_err_t (*find_all)(const regex_t *re, const char *text, size_t text_len,
                            regex_match_cb cb, void *user, size_t max_matches);
    regex_err_t (*replace)(const regex_t *re, const char *text, size_t text_len,
                           const char *replacement, int global,
                           char **out_buf, size_t *out_len);
    regex_err_t (*split)(const regex_t *re, const char *text, size_t text_len,
                         regex_split_result_t *out, size_t max_splits);
    regex_iter_t *(*iter_create)(const regex_t *re, unsigned options, regex_err_t *out_err);
    regex_err_t (*iter_feed)(regex_iter_t *it, const char *chunk, size_t len);
    regex_err_t (*iter_finish)(regex_iter_t *it);
    ssize_t (*iter_next)(regex_iter_t *it, size_t *start, size_t *end,
                         size_t *capture_offsets, size_t max_captures,
                         size_t *out_cap_count);
    regex_err_t (*iter_last_error)(const regex_iter_t *it);
    void (*iter_destroy)(regex_iter_t *it);
};

regex_err_t regex_engine_safe_init(regex_t *re, const char *pattern, unsigned flags);
const regex_engine_vtable *regex_engine_posix_vtable(void);
const regex_engine_vtable *regex_engine_safe_vtable(void);

const regex_engine_vtable *regex_engine_pcre2_vtable(void);
const regex_engine_vtable *regex_engine_re2_vtable(void);

/* util */
int regex_utf8_decode(const char *s, size_t len, size_t *index, uint32_t *out_cp);
size_t regex_utf8_encode(uint32_t cp, char out[4]);
int regex_is_newline(uint32_t cp);
uint32_t regex_ascii_tolower(uint32_t cp);
uint32_t regex_ascii_toupper(uint32_t cp);
uint32_t regex_unicode_tolower(uint32_t cp);
uint32_t regex_unicode_toupper(uint32_t cp);

#endif /* SUBSTRATE_REGEX_INTERNAL_H */
