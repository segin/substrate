#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ls_sort.h"

static const ls_config_t *current_config = NULL;

// Natural version sort comparison (strverscmp-like)
static int version_compare(const char *a, const char *b) {
    while (*a && *b) {
        if (isdigit(*a) && isdigit(*b)) {
            // Skip leading zeros
            while (*a == '0') a++;
            while (*b == '0') b++;
            
            // Count digits
            const char *da = a, *db = b;
            while (isdigit(*a)) a++;
            while (isdigit(*b)) b++;
            int len_a = a - da;
            int len_b = b - db;
            
            if (len_a != len_b) return len_a - len_b;
            while (da < a) {
                if (*da != *db) return *da - *db;
                da++; db++;
            }
        } else {
            if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
            a++; b++;
        }
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int cmp_entry(const void *a, const void *b) {
    file_info_t *fa = (file_info_t*)a;
    file_info_t *fb = (file_info_t*)b;
    int res = 0;

    if (current_config->sort_size) {
        if (fb->st.st_size > fa->st.st_size) res = 1;
        else if (fb->st.st_size < fa->st.st_size) res = -1;
    } else if (current_config->sort_time) {
        time_t ta, tb;
        switch (current_config->time_type) {
            case TIME_ATIME: ta = fa->st.st_atime; tb = fb->st.st_atime; break;
            case TIME_CTIME: ta = fa->st.st_ctime; tb = fb->st.st_ctime; break;
            default: ta = fa->st.st_mtime; tb = fb->st.st_mtime; break;
        }
        if (tb > ta) res = 1;
        else if (tb < ta) res = -1;
    } else if (current_config->version_sort) {
        res = version_compare(fa->name, fb->name);
    } else {
        res = strcmp(fa->name, fb->name);
    }

    if (current_config->reverse) res = -res;
    return res;
}

void ls_sort_entries(file_info_t *files, int count, const ls_config_t *config) {
    if (count <= 1) return;
    if (config->no_sort) return; // -U: don't sort
    current_config = config;
    qsort(files, count, sizeof(file_info_t), cmp_entry);
}
