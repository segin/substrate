#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "ls_sort.h"

typedef struct {
    const ls_config_t *config;
    bool ignore_case;
} sort_ctx_t;

static int ascii_casecmp(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;

        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return (int)ca - (int)cb;
        }

        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

static int compare_digit_run(const char **ap, const char **bp) {
    const char *a = *ap;
    const char *b = *bp;
    const char *a_sig = a;
    const char *b_sig = b;
    size_t a_len;
    size_t b_len;

    while (*a_sig == '0') {
        a_sig++;
    }
    while (*b_sig == '0') {
        b_sig++;
    }

    while (isdigit((unsigned char)*a)) {
        a++;
    }
    while (isdigit((unsigned char)*b)) {
        b++;
    }

    a_len = (size_t)(a - a_sig);
    b_len = (size_t)(b - b_sig);

    *ap = a;
    *bp = b;

    if (a_len != b_len) {
        return (a_len < b_len) ? -1 : 1;
    }

    while (a_sig < a && b_sig < b) {
        if (*a_sig != *b_sig) {
            return (unsigned char)*a_sig - (unsigned char)*b_sig;
        }
        a_sig++;
        b_sig++;
    }

    return 0;
}

static int version_compare(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            int rc = compare_digit_run(&a, &b);
            if (rc != 0) {
                return rc;
            }
            continue;
        }

        if ((unsigned char)*a != (unsigned char)*b) {
            return (unsigned char)*a - (unsigned char)*b;
        }

        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

static const char *file_extension(const char *name) {
    const char *dot = NULL;
    const char *p;

    for (p = name; *p != '\0'; p++) {
        if (*p == '.') {
            dot = p;
        }
    }

    if (dot == NULL || dot == name || dot[1] == '\0') {
        return "";
    }
    return dot + 1;
}

#ifdef NATIVE_BUILD
static wchar_t *wide_lower_dup(const char *text) {
    size_t count;
    wchar_t *wide;
    size_t i;

    count = mbstowcs(NULL, text, 0);
    if (count == (size_t)-1) {
        return NULL;
    }

    wide = (wchar_t *)malloc((count + 1) * sizeof(*wide));
    if (wide == NULL) {
        return NULL;
    }

    if (mbstowcs(wide, text, count + 1) == (size_t)-1) {
        free(wide);
        return NULL;
    }

    for (i = 0; i < count; i++) {
        wide[i] = towlower(wide[i]);
    }

    return wide;
}
#endif

static int collate_name(const sort_ctx_t *ctx, const char *a, const char *b) {
#ifdef NATIVE_BUILD
    if (!ctx->ignore_case) {
        return strcoll(a, b);
    }

    {
        wchar_t *wa = wide_lower_dup(a);
        wchar_t *wb = wide_lower_dup(b);
        int rc;

        if (wa == NULL || wb == NULL) {
            free(wa);
            free(wb);
            return ascii_casecmp(a, b);
        }

        rc = wcscoll(wa, wb);
        free(wa);
        free(wb);
        return rc;
    }
#else
    if (ctx->ignore_case) {
        return ascii_casecmp(a, b);
    }
    return strcmp(a, b);
#endif
}

static time_t selected_time(const file_info_t *file, ls_time_type_t type) {
    switch (type) {
        case TIME_ATIME:
            return file->st.st_atime;
        case TIME_CTIME:
            return file->st.st_ctime;
        case TIME_MTIME:
        default:
            return file->st.st_mtime;
    }
}

static int compare_directory_bias(const sort_ctx_t *ctx, const file_info_t *a, const file_info_t *b) {
    bool a_dir;
    bool b_dir;

    if (!ctx->config->dirs_first) {
        return 0;
    }

    a_dir = S_ISDIR(a->st.st_mode);
    b_dir = S_ISDIR(b->st.st_mode);
    if (a_dir == b_dir) {
        return 0;
    }

    return a_dir ? -1 : 1;
}

static int compare_primary_key(const sort_ctx_t *ctx, const file_info_t *a, const file_info_t *b) {
    if (ctx->config->sort_size) {
        if (a->st.st_size != b->st.st_size) {
            return (a->st.st_size > b->st.st_size) ? -1 : 1;
        }
    } else if (ctx->config->sort_time) {
        time_t atime = selected_time(a, ctx->config->time_type);
        time_t btime = selected_time(b, ctx->config->time_type);

        if (atime != btime) {
            return (atime > btime) ? -1 : 1;
        }
    }

    if (ctx->config->version_sort) {
        return version_compare(a->name, b->name);
    }

    if (ctx->config->sort_extension) {
        int rc = collate_name(ctx, file_extension(a->name), file_extension(b->name));
        if (rc != 0) {
            return rc;
        }
    }

    return collate_name(ctx, a->name, b->name);
}

static int compare_entries(const sort_ctx_t *ctx, const file_info_t *a, const file_info_t *b) {
    int rc;

    rc = compare_directory_bias(ctx, a, b);
    if (rc == 0) {
        rc = compare_primary_key(ctx, a, b);
    }
    if (rc == 0) {
        rc = strcmp(a->name, b->name);
    }
    if (rc == 0) {
        if (a->input_index < b->input_index) {
            rc = -1;
        } else if (a->input_index > b->input_index) {
            rc = 1;
        }
    }

    if (ctx->config->reverse) {
        rc = -rc;
    }

    return rc;
}

static void merge_runs(file_info_t *files, file_info_t *scratch, size_t lo, size_t mid, size_t hi,
                       const sort_ctx_t *ctx) {
    size_t left = lo;
    size_t right = mid;
    size_t out = lo;

    while (left < mid && right < hi) {
        if (compare_entries(ctx, &files[left], &files[right]) <= 0) {
            scratch[out++] = files[left++];
        } else {
            scratch[out++] = files[right++];
        }
    }

    while (left < mid) {
        scratch[out++] = files[left++];
    }
    while (right < hi) {
        scratch[out++] = files[right++];
    }

    while (lo < hi) {
        files[lo] = scratch[lo];
        lo++;
    }
}

static void mergesort_impl(file_info_t *files, file_info_t *scratch, size_t lo, size_t hi,
                           const sort_ctx_t *ctx) {
    size_t mid;

    if (hi - lo <= 1) {
        return;
    }

    mid = lo + (hi - lo) / 2;
    mergesort_impl(files, scratch, lo, mid, ctx);
    mergesort_impl(files, scratch, mid, hi, ctx);
    merge_runs(files, scratch, lo, mid, hi, ctx);
}

void ls_sort_entries(file_info_t *files, size_t count, const ls_config_t *config) {
    file_info_t *scratch;
    sort_ctx_t ctx;

    if (files == NULL || config == NULL || count <= 1 || config->no_sort) {
        return;
    }

    scratch = (file_info_t *)malloc(count * sizeof(*scratch));
    if (scratch == NULL) {
        return;
    }

    ctx.config = config;
    ctx.ignore_case = config->sort_ignore_case;

    mergesort_impl(files, scratch, 0, count, &ctx);
    free(scratch);
}
