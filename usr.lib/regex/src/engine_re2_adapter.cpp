#ifdef REGEX_USE_RE2

// Include C++ headers first to avoid conflicts with local C headers
#include <re2/re2.h>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cctype>

extern "C" {
// Include internal headers inside extern "C" to ensure C linkage for internal types/functions
#include "regex_internal.h"
}

extern "C" {

typedef struct {
    re2::RE2* pattern;
    int capture_count;
} re2_regex_impl;

typedef struct {
    regex_iter_t base;
    const regex_t *re;
    std::string *buf;
    size_t offset;
    regex_err_t last_err;
    bool finished;
} re2_iter;

static regex_err_t re2_compile(regex_t *re, const char *pattern, unsigned flags) {
    re2::RE2::Options options;
    options.set_log_errors(false);

    if (flags & REGEX_FLAG_ICASE) {
        options.set_case_sensitive(false);
    }

    if (flags & REGEX_FLAG_MULTILINE) {
        options.set_one_line(false);
    } else {
        options.set_one_line(true);
    }

    if (flags & REGEX_FLAG_DOTALL) {
        options.set_dot_nl(true);
    }

    if (flags & REGEX_FLAG_LITERAL) {
        options.set_literal(true);
    }

    if (flags & REGEX_FLAG_UTF8) {
        options.set_encoding(re2::RE2::Options::EncodingUTF8);
    } else {
        options.set_encoding(re2::RE2::Options::EncodingLatin1);
    }

    re2::RE2* code = new re2::RE2(re2::StringPiece(pattern), options);
    if (!code->ok()) {
        delete code;
        return REGEX_ERR_SYNTAX;
    }

    re2_regex_impl* impl = new re2_regex_impl;
    impl->pattern = code;
    impl->capture_count = code->NumberOfCapturingGroups() + 1;

    re->impl = impl;
    re->capture_count = impl->capture_count;

    return REGEX_OK;
}

static void re2_destroy(regex_t *re) {
    re2_regex_impl* impl = (re2_regex_impl*)re->impl;
    if (impl) {
        delete impl->pattern;
        delete impl;
        re->impl = NULL;
    }
}

static ssize_t re2_match(const regex_t *re, const char *text, size_t text_len,
                         size_t *capture_offsets, size_t max_captures,
                         regex_err_t *out_err) {
    re2_regex_impl* impl = (re2_regex_impl*)re->impl;
    if (!impl) {
        if (out_err) *out_err = REGEX_ERR_INTERNAL;
        return -REGEX_ERR_INTERNAL;
    }

    re2::StringPiece input(text, text_len);
    int n_groups = impl->capture_count;
    std::vector<re2::StringPiece> submatches(n_groups);

    if (!impl->pattern->Match(input, 0, text_len, re2::RE2::UNANCHORED, submatches.data(), n_groups)) {
        if (out_err) *out_err = REGEX_OK;
        return -1;
    }

    if (capture_offsets) {
        size_t groups_to_copy = max_captures / 2;
        if (groups_to_copy > (size_t)n_groups) groups_to_copy = n_groups;

        for (size_t i = 0; i < groups_to_copy; ++i) {
            if (submatches[i].data() == NULL) {
                capture_offsets[2*i] = (size_t)-1;
                capture_offsets[2*i+1] = (size_t)-1;
            } else {
                capture_offsets[2*i] = submatches[i].data() - text;
                capture_offsets[2*i+1] = capture_offsets[2*i] + submatches[i].length();
            }
        }
    }

    if (out_err) *out_err = REGEX_OK;
    return n_groups;
}

static regex_err_t re2_find_all(const regex_t *re, const char *text, size_t text_len,
                                regex_match_cb cb, void *user, size_t max_matches) {
    re2_regex_impl* impl = (re2_regex_impl*)re->impl;
    if (!impl) return REGEX_ERR_INTERNAL;

    re2::StringPiece input(text, text_len);
    int n_groups = impl->capture_count;
    std::vector<re2::StringPiece> submatches(n_groups);
    std::vector<size_t> caps(n_groups * 2);

    size_t offset = 0;
    size_t matches = 0;
    size_t limit = max_matches ? max_matches : re->limits.max_matches;

    while (offset <= text_len) {
        if (!impl->pattern->Match(input, offset, text_len, re2::RE2::UNANCHORED, submatches.data(), n_groups)) {
            break;
        }

        for (int i = 0; i < n_groups; ++i) {
            if (submatches[i].data()) {
                caps[2*i] = submatches[i].data() - text;
                caps[2*i+1] = caps[2*i] + submatches[i].length();
            } else {
                caps[2*i] = (size_t)-1;
                caps[2*i+1] = (size_t)-1;
            }
        }

        size_t start = caps[0];
        size_t end = caps[1];

        if (!cb(user, start, end, caps.data(), n_groups)) {
            break;
        }
        matches++;
        if (limit && matches >= limit) {
            return REGEX_ERR_MATCH_TIMEOUT;
        }

        if (end == start) {
            offset = end + 1;
        } else {
            offset = end;
        }

        if (offset > text_len) break;
    }

    return REGEX_OK;
}

static regex_err_t re2_replace(const regex_t *re, const char *text, size_t text_len,
                               const char *replacement, int global,
                               char **out_buf, size_t *out_len) {
    re2_regex_impl* impl = (re2_regex_impl*)re->impl;
    if (!impl) return REGEX_ERR_INTERNAL;

    std::string s(text, text_len);

    std::string repl_str;
    size_t rlen = strlen(replacement);
    for (size_t i = 0; i < rlen; ++i) {
        if (replacement[i] == '$' && i + 1 < rlen && isdigit(replacement[i+1])) {
            repl_str += '\\';
        } else {
            repl_str += replacement[i];
        }
    }
    re2::StringPiece repl(repl_str);

    int count = 0;
    if (global) {
        count = re2::RE2::GlobalReplace(&s, *impl->pattern, repl);
    } else {
        if (re2::RE2::Replace(&s, *impl->pattern, repl)) {
            count = 1;
        }
    }
    (void)count;

    char *out = (char*)malloc(s.length() + 1);
    if (!out) return REGEX_ERR_NOMEM;

    memcpy(out, s.data(), s.length());
    out[s.length()] = '\0';

    *out_buf = out;
    if (out_len) *out_len = s.length();

    return REGEX_OK;
}

static regex_err_t re2_split(const regex_t *re, const char *text, size_t text_len,
                             regex_split_result_t *out, size_t max_splits) {
    re2_regex_impl* impl = (re2_regex_impl*)re->impl;
    if (!impl) return REGEX_ERR_INTERNAL;
    if (!out) return REGEX_ERR_INVALID_ARGUMENT;

    re2::StringPiece input(text, text_len);
    re2::StringPiece submatches[1];

    char **items = NULL;
    size_t count = 0;
    size_t cap = 0;
    size_t pos = 0;

    while (pos <= text_len) {
        if (max_splits && count >= max_splits) break;

        if (!impl->pattern->Match(input, pos, text_len, re2::RE2::UNANCHORED, submatches, 1)) {
            break;
        }

        size_t match_start = submatches[0].data() - text;
        size_t match_end = match_start + submatches[0].length();
        size_t seg_len = match_start - pos;

        if (count == cap) {
            size_t new_cap = cap ? cap * 2 : 8;
            char **new_items = (char**)realloc(items, new_cap * sizeof(char*));
            if (!new_items) {
                for(size_t i=0; i<count; ++i) free(items[i]);
                free(items);
                return REGEX_ERR_NOMEM;
            }
            items = new_items;
            cap = new_cap;
        }

        items[count] = (char*)malloc(seg_len + 1);
        if (!items[count]) {
             for(size_t i=0; i<count; ++i) free(items[i]);
             free(items);
             return REGEX_ERR_NOMEM;
        }
        memcpy(items[count], text + pos, seg_len);
        items[count][seg_len] = '\0';
        count++;

        if (match_end == match_start) {
            pos = match_end + 1;
        } else {
            pos = match_end;
        }

        if (pos > text_len) break;
    }

    if (pos <= text_len) {
        size_t seg_len = text_len - pos;
        if (count == cap) {
             size_t new_cap = cap ? cap * 2 : 8;
             char **new_items = (char**)realloc(items, new_cap * sizeof(char*));
             if (!new_items) {
                 for(size_t i=0; i<count; ++i) free(items[i]);
                 free(items);
                 return REGEX_ERR_NOMEM;
             }
             items = new_items;
             cap = new_cap;
        }
        items[count] = (char*)malloc(seg_len + 1);
        if (!items[count]) {
             for(size_t i=0; i<count; ++i) free(items[i]);
             free(items);
             return REGEX_ERR_NOMEM;
        }
        memcpy(items[count], text + pos, seg_len);
        items[count][seg_len] = '\0';
        count++;
    }

    out->items = items;
    out->count = count;

    return REGEX_OK;
}

static regex_iter_t *re2_iter_create(const regex_t *re, unsigned options, regex_err_t *out_err) {
    (void)options;
    re2_iter *it = new re2_iter;
    if (!it) {
        if (out_err) *out_err = REGEX_ERR_NOMEM;
        return NULL;
    }
    it->base.engine = regex_engine_re2_vtable();
    it->re = re;
    it->buf = new std::string;
    it->offset = 0;
    it->last_err = REGEX_OK;
    it->finished = false;

    if (out_err) *out_err = REGEX_OK;
    return &it->base;
}

static regex_err_t re2_iter_feed(regex_iter_t *it_base, const char *chunk, size_t len) {
    re2_iter *it = (re2_iter*)it_base;
    if (!it || !chunk) return REGEX_ERR_INVALID_ARGUMENT;
    if (it->finished) {
        it->last_err = REGEX_ERR_INVALID_ARGUMENT;
        return it->last_err;
    }
    try {
        it->buf->append(chunk, len);
    } catch (...) {
        it->last_err = REGEX_ERR_NOMEM;
        return it->last_err;
    }
    it->last_err = REGEX_OK;
    return REGEX_OK;
}

static regex_err_t re2_iter_finish(regex_iter_t *it_base) {
    re2_iter *it = (re2_iter*)it_base;
    if (!it) return REGEX_ERR_INVALID_ARGUMENT;
    it->finished = true;
    it->last_err = REGEX_OK;
    return REGEX_OK;
}

static ssize_t re2_iter_next(regex_iter_t *it_base, size_t *start, size_t *end,
                             size_t *capture_offsets, size_t max_captures,
                             size_t *out_cap_count) {
    re2_iter *it = (re2_iter*)it_base;
    if (!it || !it->finished) return -REGEX_ERR_INVALID_ARGUMENT;

    re2_regex_impl* impl = (re2_regex_impl*)it->re->impl;
    re2::StringPiece input(*it->buf);
    int n_groups = impl->capture_count;
    std::vector<re2::StringPiece> submatches(n_groups);

    if (it->offset > input.length()) return -1;

    if (!impl->pattern->Match(input, it->offset, input.length(), re2::RE2::UNANCHORED, submatches.data(), n_groups)) {
        return -1;
    }

    size_t match_start = submatches[0].data() - input.data();
    size_t match_end = match_start + submatches[0].length();

    if (start) *start = match_start;
    if (end) *end = match_end;

    if (capture_offsets) {
        size_t groups_to_copy = max_captures / 2;
        if (groups_to_copy > (size_t)n_groups) groups_to_copy = n_groups;

        for (size_t i = 0; i < groups_to_copy; ++i) {
            if (submatches[i].data() == NULL) {
                capture_offsets[2*i] = (size_t)-1;
                capture_offsets[2*i+1] = (size_t)-1;
            } else {
                capture_offsets[2*i] = submatches[i].data() - input.data();
                capture_offsets[2*i+1] = capture_offsets[2*i] + submatches[i].length();
            }
        }
    }

    if (out_cap_count) *out_cap_count = n_groups;

    if (match_end == it->offset) {
        it->offset++;
    } else {
        it->offset = match_end;
    }

    it->last_err = REGEX_OK;
    return n_groups;
}

static regex_err_t re2_iter_last_error(const regex_iter_t *it_base) {
    const re2_iter *it = (const re2_iter*)it_base;
    if (!it) return REGEX_ERR_INVALID_ARGUMENT;
    return it->last_err;
}

static void re2_iter_destroy(regex_iter_t *it_base) {
    re2_iter *it = (re2_iter*)it_base;
    if (it) {
        delete it->buf;
        delete it;
    }
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

} // extern "C"

#else

// Fallback if not enabled

extern "C" {
// Must include internal header to see declaration, but since it's C++ without extern C guard in header,
// we should probably just declare it here or include inside extern C.
#include "regex_internal.h"
const regex_engine_vtable *regex_engine_re2_vtable(void) {
    return NULL;
}
}

#endif
