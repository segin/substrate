#include "regex_internal.h"

#ifdef REGEX_USE_RE2
/* TODO: implement RE2 adapter if library is available */
static regex_err_t re2_compile(regex_t *re, const char *pattern, unsigned flags) {
    (void)re;
    (void)pattern;
    (void)flags;
    return REGEX_ERR_UNSUPPORTED;
}

static void re2_destroy(regex_t *re) {
    (void)re;
}

static ssize_t re2_match(const regex_t *re, const char *text, size_t text_len,
                         size_t *capture_offsets, size_t max_captures,
                         regex_err_t *out_err) {
    (void)re;
    (void)text;
    (void)text_len;
    (void)capture_offsets;
    (void)max_captures;
    if (out_err) {
        *out_err = REGEX_ERR_UNSUPPORTED;
    }
    return -REGEX_ERR_UNSUPPORTED;
}

static regex_err_t re2_find_all(const regex_t *re, const char *text, size_t text_len,
                                regex_match_cb cb, void *user, size_t max_matches) {
    (void)re;
    (void)text;
    (void)text_len;
    (void)cb;
    (void)user;
    (void)max_matches;
    return REGEX_ERR_UNSUPPORTED;
}

static regex_err_t re2_replace(const regex_t *re, const char *text, size_t text_len,
                               const char *replacement, int global,
                               char **out_buf, size_t *out_len) {
    (void)re;
    (void)text;
    (void)text_len;
    (void)replacement;
    (void)global;
    (void)out_buf;
    (void)out_len;
    return REGEX_ERR_UNSUPPORTED;
}

static regex_err_t re2_split(const regex_t *re, const char *text, size_t text_len,
                             regex_split_result_t *out, size_t max_splits) {
    (void)re;
    (void)text;
    (void)text_len;
    (void)out;
    (void)max_splits;
    return REGEX_ERR_UNSUPPORTED;
}

static regex_iter_t *re2_iter_create(const regex_t *re, unsigned options, regex_err_t *out_err) {
    (void)re;
    (void)options;
    if (out_err) {
        *out_err = REGEX_ERR_UNSUPPORTED;
    }
    return NULL;
}

static regex_err_t re2_iter_feed(regex_iter_t *it, const char *chunk, size_t len) {
    (void)it;
    (void)chunk;
    (void)len;
    return REGEX_ERR_UNSUPPORTED;
}

static regex_err_t re2_iter_finish(regex_iter_t *it) {
    (void)it;
    return REGEX_ERR_UNSUPPORTED;
}

static ssize_t re2_iter_next(regex_iter_t *it, size_t *start, size_t *end,
                             size_t *capture_offsets, size_t max_captures,
                             size_t *out_cap_count) {
    (void)it;
    (void)start;
    (void)end;
    (void)capture_offsets;
    (void)max_captures;
    (void)out_cap_count;
    return -REGEX_ERR_UNSUPPORTED;
}

static regex_err_t re2_iter_last_error(const regex_iter_t *it) {
    (void)it;
    return REGEX_ERR_UNSUPPORTED;
}

static void re2_iter_destroy(regex_iter_t *it) {
    (void)it;
}

static const regex_engine_vtable re2_vtable = {
    .name = "re2",
    .compile = re2_compile,
    .destroy = re2_destroy,
    .match = re2_match,
    .find_all = re2_find_all,
    .replace = re2_replace,
    .split = re2_split,
    .iter_create = re2_iter_create,
    .iter_feed = re2_iter_feed,
    .iter_finish = re2_iter_finish,
    .iter_next = re2_iter_next,
    .iter_last_error = re2_iter_last_error,
    .iter_destroy = re2_iter_destroy
};

const regex_engine_vtable *regex_engine_re2_vtable(void) {
    return &re2_vtable;
}

#else

const regex_engine_vtable *regex_engine_re2_vtable(void) {
    return NULL;
}

#endif
