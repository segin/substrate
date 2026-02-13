#include "regex_internal.h"

#ifdef REGEX_USE_PCRE2
#include <pcre2.h>

typedef struct pcre2_regex_impl {
    pcre2_code *code;
    size_t capture_count;
} pcre2_regex_impl;

static uint32_t pcre2_map_flags(unsigned flags) {
    uint32_t opts = 0;
    if (flags & REGEX_FLAG_UTF8) {
        opts |= PCRE2_UTF;
    }
    if (flags & REGEX_FLAG_ICASE) {
        opts |= PCRE2_CASELESS;
    }
    if (flags & REGEX_FLAG_MULTILINE) {
        opts |= PCRE2_MULTILINE;
    }
    if (flags & REGEX_FLAG_DOTALL) {
        opts |= PCRE2_DOTALL;
    }
    if (flags & REGEX_FLAG_ANCHORED) {
        opts |= PCRE2_ANCHORED;
    }
    if (flags & REGEX_FLAG_EXTENDED) {
        opts |= PCRE2_EXTENDED;
    }
    if (flags & REGEX_FLAG_LITERAL) {
        opts |= PCRE2_LITERAL;
    }
    return opts;
}

static regex_err_t pcre2_compile(regex_t *re, const char *pattern, unsigned flags) {
    int error_code = 0;
    PCRE2_SIZE error_offset = 0;
    pcre2_code *code;
    pcre2_regex_impl *impl;
    uint32_t opts = pcre2_map_flags(flags);

    code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, opts,
                         &error_code, &error_offset, NULL);
    if (!code) {
        return REGEX_ERR_SYNTAX;
    }

    impl = (pcre2_regex_impl *)calloc(1, sizeof(*impl));
    if (!impl) {
        pcre2_code_free(code);
        return REGEX_ERR_NOMEM;
    }
    impl->code = code;
    pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &impl->capture_count);
    re->capture_count = impl->capture_count + 1;
    re->impl = impl;
    return REGEX_OK;
}

static void pcre2_destroy(regex_t *re) {
    pcre2_regex_impl *impl = (pcre2_regex_impl *)re->impl;
    if (!impl) {
        return;
    }
    pcre2_code_free(impl->code);
    free(impl);
    re->impl = NULL;
}

static ssize_t pcre2_match(const regex_t *re, const char *text, size_t text_len,
                           size_t *capture_offsets, size_t max_captures,
                           regex_err_t *out_err) {
    pcre2_regex_impl *impl = (pcre2_regex_impl *)re->impl;
    pcre2_match_data *md;
    int rc;

    if (!impl) {
        if (out_err) {
            *out_err = REGEX_ERR_INTERNAL;
        }
        return -REGEX_ERR_INTERNAL;
    }

    md = pcre2_match_data_create_from_pattern(impl->code, NULL);
    if (!md) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        return -REGEX_ERR_NOMEM;
    }

    rc = pcre2_match(impl->code, (PCRE2_SPTR)text, text_len, 0, 0, md, NULL);
    if (rc == PCRE2_ERROR_NOMATCH) {
        pcre2_match_data_free(md);
        if (out_err) {
            *out_err = REGEX_OK;
        }
        return -1;
    }
    if (rc < 0) {
        pcre2_match_data_free(md);
        if (out_err) {
            *out_err = REGEX_ERR_INTERNAL;
        }
        return -REGEX_ERR_INTERNAL;
    }

    if (capture_offsets && max_captures >= (size_t)rc * 2) {
        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(md);
        size_t i;
        for (i = 0; i < (size_t)rc * 2; ++i) {
            capture_offsets[i] = (size_t)ovector[i];
        }
    }

    pcre2_match_data_free(md);
    if (out_err) {
        *out_err = REGEX_OK;
    }
    return rc;
}

static regex_err_t pcre2_find_all(const regex_t *re, const char *text, size_t text_len,
                                  regex_match_cb cb, void *user, size_t max_matches) {
    size_t offset = 0;
    size_t matches = 0;
    regex_err_t err = REGEX_OK;
    size_t cap_count;
    size_t *caps;
    ssize_t rc;
    size_t limit = max_matches ? max_matches : re->limits.max_matches;

    cap_count = regex_capture_count(re) * 2;
    caps = (size_t *)malloc(cap_count * sizeof(*caps));
    if (!caps) {
        return REGEX_ERR_NOMEM;
    }

    while (offset <= text_len) {
        rc = pcre2_match(re, text + offset, text_len - offset, caps, cap_count, &err);
        if (rc < 0 && rc != -1) {
            free(caps);
            return err;
        }
        if (rc == -1) {
            break;
        }
        if (limit && matches >= limit) {
            free(caps);
            return REGEX_ERR_MATCH_TIMEOUT;
        }
        {
            size_t start = caps[0] + offset;
            size_t end = caps[1] + offset;
            size_t i;
            for (i = 0; i < cap_count; ++i) {
                if (caps[i] != (size_t)-1) {
                    caps[i] += offset;
                }
            }
            if (!cb(user, start, end, caps, cap_count / 2)) {
                break;
            }
            matches++;
            if (end == start) {
                offset = end + 1;
            } else {
                offset = end;
            }
        }
    }

    free(caps);
    return REGEX_OK;
}

static regex_err_t pcre2_replace(const regex_t *re, const char *text, size_t text_len,
                                 const char *replacement, int global,
                                 char **out_buf, size_t *out_len) {
    pcre2_regex_impl *impl = (pcre2_regex_impl *)re->impl;
    uint32_t opts = 0;
    PCRE2_SIZE out_size = 0;
    int rc;
    char *out;

    if (!impl || !out_buf) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }

    if (global) {
        opts |= PCRE2_SUBSTITUTE_GLOBAL;
    }

    rc = pcre2_substitute(impl->code, (PCRE2_SPTR)text, text_len, 0, opts, NULL, NULL,
                          (PCRE2_SPTR)replacement, PCRE2_ZERO_TERMINATED,
                          NULL, &out_size);
    if (rc != PCRE2_ERROR_NOMEMORY) {
        return REGEX_ERR_INTERNAL;
    }

    out = (char *)malloc(out_size + 1);
    if (!out) {
        return REGEX_ERR_NOMEM;
    }

    rc = pcre2_substitute(impl->code, (PCRE2_SPTR)text, text_len, 0, opts, NULL, NULL,
                          (PCRE2_SPTR)replacement, PCRE2_ZERO_TERMINATED,
                          (PCRE2_UCHAR *)out, &out_size);
    if (rc < 0) {
        free(out);
        return REGEX_ERR_INTERNAL;
    }

    out[out_size] = '\0';
    *out_buf = out;
    if (out_len) {
        *out_len = (size_t)out_size;
    }
    return REGEX_OK;
}

static regex_err_t pcre2_split(const regex_t *re, const char *text, size_t text_len,
                               regex_split_result_t *out, size_t max_splits) {
    regex_err_t err = REGEX_OK;
    size_t cap_count = regex_capture_count(re) * 2;
    size_t *caps;
    size_t pos = 0;
    size_t count = 0;
    size_t cap = 0;
    char **items = NULL;
    ssize_t rc;

    if (!out) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }

    caps = (size_t *)malloc(cap_count * sizeof(*caps));
    if (!caps) {
        return REGEX_ERR_NOMEM;
    }

    while (pos <= text_len) {
        rc = pcre2_match(re, text + pos, text_len - pos, caps, cap_count, &err);
        if (rc < 0 && rc != -1) {
            free(caps);
            return err;
        }
        if (rc == -1 || (max_splits && count >= max_splits)) {
            break;
        }
        {
            size_t start = caps[0] + pos;
            size_t end = caps[1] + pos;
            size_t seg_len = start - pos;
            if (count == cap) {
                size_t new_cap = cap ? cap * 2 : 8;
                char **new_items = (char **)realloc(items, new_cap * sizeof(*new_items));
                if (!new_items) {
                    free(caps);
                    return REGEX_ERR_NOMEM;
                }
                items = new_items;
                cap = new_cap;
            }
            items[count] = (char *)malloc(seg_len + 1);
            if (!items[count]) {
                free(caps);
                return REGEX_ERR_NOMEM;
            }
            memcpy(items[count], text + pos, seg_len);
            items[count][seg_len] = '\0';
            count++;
            pos = end;
            if (end == start) {
                pos++;
            }
        }
    }

    if (pos <= text_len) {
        size_t seg_len = text_len - pos;
        if (count == cap) {
            size_t new_cap = cap ? cap * 2 : 8;
            char **new_items = (char **)realloc(items, new_cap * sizeof(*new_items));
            if (!new_items) {
                free(caps);
                return REGEX_ERR_NOMEM;
            }
            items = new_items;
            cap = new_cap;
        }
        items[count] = (char *)malloc(seg_len + 1);
        if (!items[count]) {
            free(caps);
            return REGEX_ERR_NOMEM;
        }
        memcpy(items[count], text + pos, seg_len);
        items[count][seg_len] = '\0';
        count++;
    }

    out->items = items;
    out->count = count;
    free(caps);
    return REGEX_OK;
}

static regex_iter_t *pcre2_iter_create(const regex_t *re, unsigned options, regex_err_t *out_err) {
    (void)re;
    (void)options;
    if (out_err) {
        *out_err = REGEX_ERR_UNSUPPORTED;
    }
    return NULL;
}

static regex_err_t pcre2_iter_feed(regex_iter_t *it, const char *chunk, size_t len) {
    (void)it;
    (void)chunk;
    (void)len;
    return REGEX_ERR_UNSUPPORTED;
}

static regex_err_t pcre2_iter_finish(regex_iter_t *it) {
    (void)it;
    return REGEX_ERR_UNSUPPORTED;
}

static ssize_t pcre2_iter_next(regex_iter_t *it, size_t *start, size_t *end,
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

static regex_err_t pcre2_iter_last_error(const regex_iter_t *it) {
    (void)it;
    return REGEX_ERR_UNSUPPORTED;
}

static void pcre2_iter_destroy(regex_iter_t *it) {
    (void)it;
}

static const regex_engine_vtable pcre2_vtable = {
    .name = "pcre2",
    .compile = pcre2_compile,
    .destroy = pcre2_destroy,
    .match = pcre2_match,
    .find_all = pcre2_find_all,
    .replace = pcre2_replace,
    .split = pcre2_split,
    .iter_create = pcre2_iter_create,
    .iter_feed = pcre2_iter_feed,
    .iter_finish = pcre2_iter_finish,
    .iter_next = pcre2_iter_next,
    .iter_last_error = pcre2_iter_last_error,
    .iter_destroy = pcre2_iter_destroy
};

const regex_engine_vtable *regex_engine_pcre2_vtable(void) {
    return &pcre2_vtable;
}

#else

const regex_engine_vtable *regex_engine_pcre2_vtable(void) {
    return NULL;
}

#endif
