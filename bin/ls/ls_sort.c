#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "ls_sort.h"

static int version_compare(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            const char *sa = a;
            const char *sb = b;
            int len_a;
            int len_b;

            while (*sa == '0') sa++;
            while (*sb == '0') sb++;

            a = sa;
            b = sb;

            while (isdigit((unsigned char)*a)) a++;
            while (isdigit((unsigned char)*b)) b++;

            len_a = (int)(a - sa);
            len_b = (int)(b - sb);

            if (len_a != len_b) {
                return len_a - len_b;
            }

            while (sa < a && sb < b) {
                if (*sa != *sb) {
                    return (unsigned char)*sa - (unsigned char)*sb;
                }
                sa++;
                sb++;
            }
        } else {
            if (*a != *b) {
                return (unsigned char)*a - (unsigned char)*b;
            }
            a++;
            b++;
        }
    }

    return (unsigned char)*a - (unsigned char)*b;
}

static int ascii_casecmp(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

#ifdef NATIVE_BUILD
static wchar_t *wide_lower_dup(const char *s) {
    size_t n;
    wchar_t *out;
    size_t i;

    n = mbstowcs(NULL, s, 0);
    if (n == (size_t)-1) {
        return NULL;
    }

    out = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }

    if (mbstowcs(out, s, n + 1) == (size_t)-1) {
        free(out);
        return NULL;
    }

    for (i = 0; i < n; i++) {
        out[i] = towlower(out[i]);
    }

    return out;
}
#endif

static int collate_name(const char *a, const char *b, int ignore_case) {
#ifdef NATIVE_BUILD
    if (!ignore_case) {
        return strcoll(a, b);
    }
    {
        wchar_t *la = wide_lower_dup(a);
        wchar_t *lb = wide_lower_dup(b);
        int rc;
        if (la == NULL || lb == NULL) {
            free(la);
            free(lb);
            return ascii_casecmp(a, b);
        }
        rc = wcscoll(la, lb);
        free(la);
        free(lb);
        return rc;
    }
#else
    if (ignore_case) {
        return ascii_casecmp(a, b);
    }
    return strcmp(a, b);
#endif
}

static const char *file_ext(const char *name) {
    const char *base = name;
    const char *dot = NULL;

    while (*base != '\0') {
        if (*base == '.') {
            dot = base;
        }
        base++;
    }

    if (dot == NULL || dot == name || dot[1] == '\0') {
        return "";
    }
    return dot + 1;
}

static time_t select_time(const file_info_t *f, ls_time_type_t tt) {
    if (tt == TIME_ATIME) return f->st.st_atime;
    if (tt == TIME_CTIME) return f->st.st_ctime;
    return f->st.st_mtime;
}

static int compare_core(const file_info_t *a, const file_info_t *b, const ls_config_t *config) {
    if (config->dirs_first) {
        int ad = S_ISDIR(a->st.st_mode) ? 1 : 0;
        int bd = S_ISDIR(b->st.st_mode) ? 1 : 0;
        if (ad != bd) {
            return bd - ad;
        }
    }

    if (config->sort_size) {
        if (a->st.st_size < b->st.st_size) return 1;
        if (a->st.st_size > b->st.st_size) return -1;
    } else if (config->sort_time) {
        time_t ta = select_time(a, config->time_type);
        time_t tb = select_time(b, config->time_type);
        if (ta < tb) return 1;
        if (ta > tb) return -1;
    }

    if (config->version_sort) {
        return version_compare(a->name, b->name);
    }

    if (config->sort_extension) {
        int ext_cmp = collate_name(file_ext(a->name), file_ext(b->name), config->sort_ignore_case ? 1 : 0);
        if (ext_cmp != 0) {
            return ext_cmp;
        }
    }

    return collate_name(a->name, b->name, config->sort_ignore_case ? 1 : 0);
}

static int compare_entry(const file_info_t *a, const file_info_t *b, const ls_config_t *config) {
    int cmp = compare_core(a, b, config);

    if (cmp == 0) {
        cmp = strcmp(a->name, b->name);
    }
    if (cmp == 0) {
        if (a->input_index < b->input_index) cmp = -1;
        else if (a->input_index > b->input_index) cmp = 1;
    }

    if (config->reverse) {
        cmp = -cmp;
    }

    return cmp;
}

static void merge_range(file_info_t *arr, file_info_t *tmp, size_t lo, size_t mid, size_t hi,
                        const ls_config_t *config) {
    size_t i = lo;
    size_t j = mid;
    size_t k = lo;

    while (i < mid && j < hi) {
        if (compare_entry(&arr[i], &arr[j], config) <= 0) tmp[k++] = arr[i++];
        else tmp[k++] = arr[j++];
    }

    while (i < mid) tmp[k++] = arr[i++];
    while (j < hi) tmp[k++] = arr[j++];

    for (k = lo; k < hi; k++) arr[k] = tmp[k];
}

static void mergesort_recursive(file_info_t *arr, file_info_t *tmp, size_t lo, size_t hi,
                                const ls_config_t *config) {
    size_t mid;

    if (hi - lo <= 1) return;

    mid = lo + (hi - lo) / 2;
    mergesort_recursive(arr, tmp, lo, mid, config);
    mergesort_recursive(arr, tmp, mid, hi, config);
    merge_range(arr, tmp, lo, mid, hi, config);
}

void ls_sort_entries(file_info_t *files, size_t count, const ls_config_t *config) {
    file_info_t *tmp;

    if (files == NULL || config == NULL || count <= 1 || config->no_sort) {
        return;
    }

    tmp = (file_info_t *)malloc(sizeof(file_info_t) * count);
    if (tmp == NULL) {
        return;
    }

    mergesort_recursive(files, tmp, 0, count, config);
    free(tmp);
}
