#include <stdlib.h>
#include <string.h>
#include "ls_sort.h"

static const ls_config_t *current_config = NULL;

static int cmp_entry(const void *a, const void *b) {
    file_info_t *fa = (file_info_t*)a;
    file_info_t *fb = (file_info_t*)b;
    int res = 0;

    if (current_config->sort_size) {
        if (fb->st.st_size > fa->st.st_size) res = 1;
        else if (fb->st.st_size < fa->st.st_size) res = -1;
    } else if (current_config->sort_time) {
        if (fb->st.st_mtime > fa->st.st_mtime) res = 1;
        else if (fb->st.st_mtime < fa->st.st_mtime) res = -1;
    } else {
        res = strcmp(fa->name, fb->name);
    }

    if (current_config->reverse) res = -res;
    return res;
}

void ls_sort_entries(file_info_t *files, int count, const ls_config_t *config) {
    if (count <= 1) return;
    current_config = config;
    qsort(files, count, sizeof(file_info_t), cmp_entry);
}
