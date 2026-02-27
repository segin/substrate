#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ls_sort.h"

static int version_compare(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            const char *sa = a;
            const char *sb = b;
            int len_a;
            int len_b;

            while (*sa == '0') {
                sa++;
            }
            while (*sb == '0') {
                sb++;
            }

            a = sa;
            b = sb;

            while (isdigit((unsigned char)*a)) {
                a++;
            }
            while (isdigit((unsigned char)*b)) {
                b++;
            }

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

static time_t select_time(const file_info_t *f, ls_time_type_t tt) {
    if (tt == TIME_ATIME) {
        return f->st.st_atime;
    }
    if (tt == TIME_CTIME) {
        return f->st.st_ctime;
    }
    return f->st.st_mtime;
}

static int compare_core(const file_info_t *a, const file_info_t *b, const ls_config_t *config) {
    if (config->sort_size) {
        if (a->st.st_size < b->st.st_size) {
            return 1;
        }
        if (a->st.st_size > b->st.st_size) {
            return -1;
        }
    } else if (config->sort_time) {
        time_t ta = select_time(a, config->time_type);
        time_t tb = select_time(b, config->time_type);
        if (ta < tb) {
            return 1;
        }
        if (ta > tb) {
            return -1;
        }
    }

    if (config->version_sort) {
        return version_compare(a->name, b->name);
    }

#ifdef NATIVE_BUILD
    return strcoll(a->name, b->name);
#else
    return strcmp(a->name, b->name);
#endif
}

static int compare_entry(const file_info_t *a, const file_info_t *b, const ls_config_t *config) {
    int cmp = compare_core(a, b, config);

    if (cmp == 0) {
        cmp = strcmp(a->name, b->name);
    }
    if (cmp == 0) {
        if (a->input_index < b->input_index) {
            cmp = -1;
        } else if (a->input_index > b->input_index) {
            cmp = 1;
        }
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
        if (compare_entry(&arr[i], &arr[j], config) <= 0) {
            tmp[k++] = arr[i++];
        } else {
            tmp[k++] = arr[j++];
        }
    }

    while (i < mid) {
        tmp[k++] = arr[i++];
    }
    while (j < hi) {
        tmp[k++] = arr[j++];
    }

    for (k = lo; k < hi; k++) {
        arr[k] = tmp[k];
    }
}

static void mergesort_recursive(file_info_t *arr, file_info_t *tmp, size_t lo, size_t hi,
                                const ls_config_t *config) {
    size_t mid;

    if (hi - lo <= 1) {
        return;
    }

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
