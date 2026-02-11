#include <stdlib.h>

#include "regex_internal.h"

ssize_t regex_match(const regex_t *re, const char *text, size_t text_len,
                    size_t *capture_offsets, size_t max_captures,
                    regex_err_t *out_err) {
    if (!re || !re->engine || !re->engine->match || !text) {
        if (out_err) {
            *out_err = REGEX_ERR_INVALID_ARGUMENT;
        }
        return -REGEX_ERR_INVALID_ARGUMENT;
    }
    return re->engine->match(re, text, text_len, capture_offsets, max_captures, out_err);
}

regex_err_t regex_find_all(const regex_t *re, const char *text, size_t text_len,
                           regex_match_cb cb, void *user, size_t max_matches) {
    if (!re || !re->engine || !re->engine->find_all || !text || !cb) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    return re->engine->find_all(re, text, text_len, cb, user, max_matches);
}

regex_err_t regex_replace(const regex_t *re, const char *text, size_t text_len,
                          const char *replacement, int global,
                          char **out_buf, size_t *out_len) {
    if (!re || !re->engine || !re->engine->replace || !text || !replacement || !out_buf) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    return re->engine->replace(re, text, text_len, replacement, global, out_buf, out_len);
}

regex_err_t regex_split(const regex_t *re, const char *text, size_t text_len,
                        regex_split_result_t *out, size_t max_splits) {
    if (!re || !re->engine || !re->engine->split || !text || !out) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    return re->engine->split(re, text, text_len, out, max_splits);
}

void regex_split_free(regex_split_result_t *result) {
    size_t i;
    if (!result || !result->items) {
        return;
    }
    for (i = 0; i < result->count; ++i) {
        free(result->items[i]);
    }
    free(result->items);
    result->items = NULL;
    result->count = 0;
}

regex_iter_t *regex_iter_create(const regex_t *re, unsigned options, regex_err_t *out_err) {
    if (!re || !re->engine || !re->engine->iter_create) {
        if (out_err) {
            *out_err = REGEX_ERR_INVALID_ARGUMENT;
        }
        return NULL;
    }
    return re->engine->iter_create(re, options, out_err);
}

regex_err_t regex_iter_feed(regex_iter_t *it, const char *chunk, size_t len) {
    if (!it || !chunk) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    return it->engine->iter_feed(it, chunk, len);
}

regex_err_t regex_iter_finish(regex_iter_t *it) {
    if (!it) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    return it->engine->iter_finish(it);
}

ssize_t regex_iter_next(regex_iter_t *it, size_t *start, size_t *end,
                        size_t *capture_offsets, size_t max_captures,
                        size_t *out_cap_count) {
    if (!it) {
        return -REGEX_ERR_INVALID_ARGUMENT;
    }
    return it->engine->iter_next(it, start, end, capture_offsets, max_captures, out_cap_count);
}

regex_err_t regex_iter_last_error(const regex_iter_t *it) {
    if (!it) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    return it->engine->iter_last_error(it);
}

void regex_iter_destroy(regex_iter_t *it) {
    if (!it) {
        return;
    }
    it->engine->iter_destroy(it);
}
