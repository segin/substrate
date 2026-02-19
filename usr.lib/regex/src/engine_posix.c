#include <stdlib.h>
#include <string.h>

#include "regex_internal.h"

#ifdef REGEX_USE_HOST_POSIX

#define regex_t host_regex_t
#include </usr/include/regex.h>
#undef regex_t

typedef struct posix_regex_impl {
    host_regex_t re;
} posix_regex_impl;

typedef struct posix_iter {
    regex_iter_t base;
    const regex_t *re;
    char *buf;
    size_t len;
    size_t cap;
    size_t offset;
    regex_err_t last_err;
    int finished;
} posix_iter;

enum {
    MATCH_ERR = -1,
    MATCH_NONE = 0,
    MATCH_OK = 1
};

static int append_buf(char **buf, size_t *len, size_t *cap, const char *src, size_t n) {
    char *next;
    size_t new_cap;

    if (!buf || !len || !cap || (!src && n)) {
        return 0;
    }

    if (*len + n + 1 > *cap) {
        new_cap = *cap ? *cap : 64;
        while (new_cap < *len + n + 1) {
            new_cap *= 2;
        }
        next = (char *)realloc(*buf, new_cap);
        if (!next) {
            return 0;
        }
        *buf = next;
        *cap = new_cap;
    }

    if (n) {
        memcpy(*buf + *len, src, n);
        *len += n;
    }
    (*buf)[*len] = '\0';
    return 1;
}

static int posix_match_from(const regex_t *re, const char *text, size_t text_len,
                            size_t offset, regmatch_t *m, size_t mcount,
                            regex_err_t *out_err) {
    posix_regex_impl *impl;
    char *tmp;
    int rc;
    size_t i;

    if (!re || !re->impl || !text || !m || offset > text_len) {
        if (out_err) {
            *out_err = REGEX_ERR_INVALID_ARGUMENT;
        }
        return MATCH_ERR;
    }

    impl = (posix_regex_impl *)re->impl;
    tmp = (char *)malloc(text_len - offset + 1);
    if (!tmp) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        return MATCH_ERR;
    }

    memcpy(tmp, text + offset, text_len - offset);
    tmp[text_len - offset] = '\0';

    rc = regexec(&impl->re, tmp, mcount, m, 0);
    free(tmp);

    if (rc == REG_NOMATCH) {
        if (out_err) {
            *out_err = REGEX_OK;
        }
        return MATCH_NONE;
    }
    if (rc != 0) {
        if (out_err) {
            *out_err = REGEX_ERR_INTERNAL;
        }
        return MATCH_ERR;
    }

    for (i = 0; i < mcount; ++i) {
        if (m[i].rm_so >= 0) {
            m[i].rm_so += (regoff_t)offset;
        }
        if (m[i].rm_eo >= 0) {
            m[i].rm_eo += (regoff_t)offset;
        }
    }

    if (out_err) {
        *out_err = REGEX_OK;
    }
    return MATCH_OK;
}

static regex_err_t posix_compile(regex_t *re, const char *pattern, unsigned flags) {
    posix_regex_impl *impl;
    int cflags = 0;
    int rc;
    char *final_pattern;

    if (!re || !pattern) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }

    if (flags & REGEX_FLAG_EXTENDED) {
        cflags |= REG_EXTENDED;
    }
    if (flags & REGEX_FLAG_ICASE) {
        cflags |= REG_ICASE;
    }
    if (flags & REGEX_FLAG_MULTILINE) {
        cflags |= REG_NEWLINE;
    }

    if (flags & REGEX_FLAG_LITERAL) {
        final_pattern = regex_escape_literal(pattern, strlen(pattern));
    } else {
        final_pattern = strdup(pattern);
    }
    if (!final_pattern) {
        return REGEX_ERR_NOMEM;
    }

    impl = (posix_regex_impl *)calloc(1, sizeof(*impl));
    if (!impl) {
        free(final_pattern);
        return REGEX_ERR_NOMEM;
    }

    rc = regcomp(&impl->re, final_pattern, cflags);
    free(final_pattern);
    if (rc != 0) {
        free(impl);
        return REGEX_ERR_SYNTAX;
    }

    re->impl = impl;
    re->capture_count = impl->re.re_nsub + 1;
    if (re->capture_count == 0) {
        re->capture_count = 1;
    }

    return REGEX_OK;
}

static void posix_destroy(regex_t *re) {
    posix_regex_impl *impl;

    if (!re || !re->impl) {
        return;
    }

    impl = (posix_regex_impl *)re->impl;
    regfree(&impl->re);
    free(impl);
    re->impl = NULL;
}

static ssize_t posix_match(const regex_t *re, const char *text, size_t text_len,
                           size_t *capture_offsets, size_t max_captures,
                           regex_err_t *out_err) {
    regmatch_t *m;
    size_t cap_count;
    size_t i;
    int mr;

    if (!re || !text) {
        if (out_err) {
            *out_err = REGEX_ERR_INVALID_ARGUMENT;
        }
        return -REGEX_ERR_INVALID_ARGUMENT;
    }

    cap_count = re->capture_count ? re->capture_count : 1;
    m = (regmatch_t *)calloc(cap_count, sizeof(*m));
    if (!m) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        return -REGEX_ERR_NOMEM;
    }

    mr = posix_match_from(re, text, text_len, 0, m, cap_count, out_err);
    if (mr == MATCH_NONE) {
        free(m);
        return -1;
    }
    if (mr == MATCH_ERR) {
        free(m);
        return -REGEX_ERR_INTERNAL;
    }

    if ((re->flags & REGEX_FLAG_ANCHORED) && m[0].rm_so != 0) {
        free(m);
        if (out_err) {
            *out_err = REGEX_OK;
        }
        return -1;
    }

    if (capture_offsets && max_captures >= cap_count * 2) {
        for (i = 0; i < cap_count; ++i) {
            capture_offsets[2 * i] = (m[i].rm_so >= 0) ? (size_t)m[i].rm_so : (size_t)-1;
            capture_offsets[2 * i + 1] = (m[i].rm_eo >= 0) ? (size_t)m[i].rm_eo : (size_t)-1;
        }
    }

    free(m);
    if (out_err) {
        *out_err = REGEX_OK;
    }
    return (ssize_t)cap_count;
}

static regex_err_t posix_find_all(const regex_t *re, const char *text, size_t text_len,
                                  regex_match_cb cb, void *user, size_t max_matches) {
    regmatch_t *m;
    size_t cap_count;
    size_t offset = 0;
    size_t matched = 0;

    if (!re || !text || !cb) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }

    cap_count = re->capture_count ? re->capture_count : 1;
    m = (regmatch_t *)calloc(cap_count, sizeof(*m));
    if (!m) {
        return REGEX_ERR_NOMEM;
    }

    while (offset <= text_len) {
        int mr;
        size_t i;
        size_t *caps;
        size_t start;
        size_t end;

        mr = posix_match_from(re, text, text_len, offset, m, cap_count, NULL);
        if (mr == MATCH_NONE) {
            break;
        }
        if (mr == MATCH_ERR) {
            free(m);
            return REGEX_ERR_INTERNAL;
        }

        start = (size_t)m[0].rm_so;
        end = (size_t)m[0].rm_eo;

        caps = (size_t *)malloc(cap_count * 2 * sizeof(*caps));
        if (!caps) {
            free(m);
            return REGEX_ERR_NOMEM;
        }

        for (i = 0; i < cap_count; ++i) {
            caps[2 * i] = (m[i].rm_so >= 0) ? (size_t)m[i].rm_so : (size_t)-1;
            caps[2 * i + 1] = (m[i].rm_eo >= 0) ? (size_t)m[i].rm_eo : (size_t)-1;
        }

        if (!cb(user, start, end, caps, cap_count)) {
            free(caps);
            break;
        }
        free(caps);

        matched++;
        if (max_matches && matched >= max_matches) {
            break;
        }

        if (end == offset) {
            offset++;
        } else {
            offset = end;
        }
    }

    free(m);
    return REGEX_OK;
}

static int append_repl(char **out, size_t *out_len, size_t *out_cap,
                       const char *replacement, const char *text,
                       const regmatch_t *m, size_t mcount) {
    size_t i;

    for (i = 0; replacement[i] != '\0'; ++i) {
        if ((replacement[i] == '$' || replacement[i] == '\\') &&
            replacement[i + 1] >= '0' && replacement[i + 1] <= '9') {
            size_t idx = (size_t)(replacement[i + 1] - '0');
            if (idx < mcount && m[idx].rm_so >= 0 && m[idx].rm_eo >= m[idx].rm_so) {
                size_t n = (size_t)(m[idx].rm_eo - m[idx].rm_so);
                if (!append_buf(out, out_len, out_cap, text + m[idx].rm_so, n)) {
                    return 0;
                }
            }
            ++i;
            continue;
        }

        if (!append_buf(out, out_len, out_cap, replacement + i, 1)) {
            return 0;
        }
    }

    return 1;
}

static regex_err_t posix_replace(const regex_t *re, const char *text, size_t text_len,
                                 const char *replacement, int global,
                                 char **out_buf, size_t *out_len) {
    regmatch_t *m;
    size_t cap_count;
    size_t offset = 0;
    char *out = NULL;
    size_t out_sz = 0;
    size_t out_cap = 0;
    int did_replace = 0;

    if (!re || !text || !replacement || !out_buf) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }

    cap_count = re->capture_count ? re->capture_count : 1;
    m = (regmatch_t *)calloc(cap_count, sizeof(*m));
    if (!m) {
        return REGEX_ERR_NOMEM;
    }

    while (offset <= text_len) {
        int mr;
        size_t start;
        size_t end;

        mr = posix_match_from(re, text, text_len, offset, m, cap_count, NULL);
        if (mr == MATCH_NONE) {
            break;
        }
        if (mr == MATCH_ERR) {
            free(m);
            free(out);
            return REGEX_ERR_INTERNAL;
        }

        start = (size_t)m[0].rm_so;
        end = (size_t)m[0].rm_eo;

        if (!append_buf(&out, &out_sz, &out_cap, text + offset, start - offset)) {
            free(m);
            free(out);
            return REGEX_ERR_NOMEM;
        }

        if (!append_repl(&out, &out_sz, &out_cap, replacement, text, m, cap_count)) {
            free(m);
            free(out);
            return REGEX_ERR_NOMEM;
        }

        did_replace = 1;
        offset = end;
        if (!global) {
            break;
        }
        if (end == start) {
            offset++;
        }
    }

    if (!append_buf(&out, &out_sz, &out_cap, text + offset, text_len - offset)) {
        free(m);
        free(out);
        return REGEX_ERR_NOMEM;
    }

    if (!did_replace && !out) {
        out = strdup(text);
        if (!out) {
            free(m);
            return REGEX_ERR_NOMEM;
        }
        out_sz = text_len;
    }

    free(m);
    *out_buf = out;
    if (out_len) {
        *out_len = out_sz;
    }
    return REGEX_OK;
}

static regex_err_t posix_split(const regex_t *re, const char *text, size_t text_len,
                               regex_split_result_t *out, size_t max_splits) {
    regmatch_t *m;
    size_t cap_count;
    size_t offset = 0;
    size_t count = 0;
    size_t cap = 8;
    char **items;

    if (!re || !text || !out) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }

    cap_count = re->capture_count ? re->capture_count : 1;
    m = (regmatch_t *)calloc(cap_count, sizeof(*m));
    items = (char **)calloc(cap, sizeof(*items));
    if (!m || !items) {
        free(m);
        free(items);
        return REGEX_ERR_NOMEM;
    }

    while (offset <= text_len) {
        int mr;
        size_t start;
        size_t end;
        size_t seg_len;

        mr = posix_match_from(re, text, text_len, offset, m, cap_count, NULL);
        if (mr == MATCH_NONE) {
            break;
        }
        if (mr == MATCH_ERR) {
            free(m);
            while (count) {
                free(items[--count]);
            }
            free(items);
            return REGEX_ERR_INTERNAL;
        }

        start = (size_t)m[0].rm_so;
        end = (size_t)m[0].rm_eo;
        seg_len = start - offset;

        if (count == cap) {
            char **next = (char **)realloc(items, cap * 2 * sizeof(*items));
            if (!next) {
                free(m);
                while (count) {
                    free(items[--count]);
                }
                free(items);
                return REGEX_ERR_NOMEM;
            }
            cap *= 2;
            items = next;
        }

        items[count] = (char *)malloc(seg_len + 1);
        if (!items[count]) {
            free(m);
            while (count) {
                free(items[--count]);
            }
            free(items);
            return REGEX_ERR_NOMEM;
        }
        memcpy(items[count], text + offset, seg_len);
        items[count][seg_len] = '\0';
        count++;

        if (max_splits && count >= max_splits) {
            offset = end;
            break;
        }

        offset = end;
        if (end == start) {
            offset++;
        }
    }

    if (count == cap) {
        char **next = (char **)realloc(items, (cap + 1) * sizeof(*items));
        if (!next) {
            free(m);
            while (count) {
                free(items[--count]);
            }
            free(items);
            return REGEX_ERR_NOMEM;
        }
        items = next;
    }

    items[count] = (char *)malloc(text_len - offset + 1);
    if (!items[count]) {
        free(m);
        while (count) {
            free(items[--count]);
        }
        free(items);
        return REGEX_ERR_NOMEM;
    }
    memcpy(items[count], text + offset, text_len - offset);
    items[count][text_len - offset] = '\0';
    count++;

    free(m);
    out->items = items;
    out->count = count;
    return REGEX_OK;
}

static regex_iter_t *posix_iter_create(const regex_t *re, unsigned options, regex_err_t *out_err) {
    posix_iter *it;

    (void)options;

    if (!re) {
        if (out_err) {
            *out_err = REGEX_ERR_INVALID_ARGUMENT;
        }
        return NULL;
    }

    it = (posix_iter *)calloc(1, sizeof(*it));
    if (!it) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        return NULL;
    }

    it->base.engine = regex_engine_posix_vtable();
    it->re = re;
    it->last_err = REGEX_OK;

    if (out_err) {
        *out_err = REGEX_OK;
    }
    return &it->base;
}

static regex_err_t posix_iter_feed(regex_iter_t *it_base, const char *chunk, size_t len) {
    posix_iter *it = (posix_iter *)it_base;

    if (!it || !chunk) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    if (it->finished) {
        it->last_err = REGEX_ERR_INVALID_ARGUMENT;
        return it->last_err;
    }

    if (!append_buf(&it->buf, &it->len, &it->cap, chunk, len)) {
        it->last_err = REGEX_ERR_NOMEM;
        return it->last_err;
    }

    it->last_err = REGEX_OK;
    return REGEX_OK;
}

static regex_err_t posix_iter_finish(regex_iter_t *it_base) {
    posix_iter *it = (posix_iter *)it_base;

    if (!it) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }

    it->finished = 1;
    it->last_err = REGEX_OK;
    return REGEX_OK;
}

static ssize_t posix_iter_next(regex_iter_t *it_base, size_t *start, size_t *end,
                               size_t *capture_offsets, size_t max_captures,
                               size_t *out_cap_count) {
    posix_iter *it = (posix_iter *)it_base;
    regmatch_t *m;
    size_t cap_count;
    size_t i;
    int mr;

    if (!it || !it->finished) {
        return -REGEX_ERR_INVALID_ARGUMENT;
    }

    cap_count = it->re->capture_count ? it->re->capture_count : 1;
    m = (regmatch_t *)calloc(cap_count, sizeof(*m));
    if (!m) {
        it->last_err = REGEX_ERR_NOMEM;
        return -REGEX_ERR_NOMEM;
    }

    mr = posix_match_from(it->re, it->buf ? it->buf : "", it->len, it->offset, m, cap_count, NULL);
    if (mr == MATCH_NONE) {
        free(m);
        return -1;
    }
    if (mr == MATCH_ERR) {
        free(m);
        it->last_err = REGEX_ERR_INTERNAL;
        return -REGEX_ERR_INTERNAL;
    }

    if (start) {
        *start = (size_t)m[0].rm_so;
    }
    if (end) {
        *end = (size_t)m[0].rm_eo;
    }

    if (capture_offsets && max_captures >= cap_count * 2) {
        for (i = 0; i < cap_count; ++i) {
            capture_offsets[2 * i] = (m[i].rm_so >= 0) ? (size_t)m[i].rm_so : (size_t)-1;
            capture_offsets[2 * i + 1] = (m[i].rm_eo >= 0) ? (size_t)m[i].rm_eo : (size_t)-1;
        }
    }

    if (out_cap_count) {
        *out_cap_count = cap_count;
    }

    if ((size_t)m[0].rm_eo == it->offset) {
        it->offset++;
    } else {
        it->offset = (size_t)m[0].rm_eo;
    }

    free(m);
    it->last_err = REGEX_OK;
    return (ssize_t)cap_count;
}

static regex_err_t posix_iter_last_error(const regex_iter_t *it_base) {
    const posix_iter *it = (const posix_iter *)it_base;
    if (!it) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    return it->last_err;
}

static void posix_iter_destroy(regex_iter_t *it_base) {
    posix_iter *it = (posix_iter *)it_base;

    if (!it) {
        return;
    }

    free(it->buf);
    free(it);
}

static const regex_engine_vtable posix_vtable = {
    .name = "posix",
    .compile = posix_compile,
    .destroy = posix_destroy,
    .match = posix_match,
    .find_all = posix_find_all,
    .replace = posix_replace,
    .split = posix_split,
    .iter_create = posix_iter_create,
    .iter_feed = posix_iter_feed,
    .iter_finish = posix_iter_finish,
    .iter_next = posix_iter_next,
    .iter_last_error = posix_iter_last_error,
    .iter_destroy = posix_iter_destroy
};

const regex_engine_vtable *regex_engine_posix_vtable(void) {
    return &posix_vtable;
}

#else

const regex_engine_vtable *regex_engine_posix_vtable(void) {
    return NULL;
}

#endif
